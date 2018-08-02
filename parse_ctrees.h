/* File: parse_ctrees.h */
/*
  This file is a part of the ``parse_ctrees`` package
  Copyright (C) 2018-- Manodeep Sinha (manodeep@gmail.com)
  License: MIT LICENSE. See LICENSE file under the top-level
  directory at https://github.com/manodeep/parse_ctrees/
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stddef.h> /* for offsetof macro*/

#include "sglib.h"


/* this is the maximum number of CTREES columns that can be requested
   (note: it is okay for the ctrees `tree_?_?_?.dat` files themselves to contain more columns) */
#define PARSE_CTREES_MAX_NCOLS      128

/* max. number of characters in a column name */
#define PARSE_CTREES_MAX_COLNAME_LEN 64

/* max. number of expected characters in one single-line */
#define PARSE_CTREES_MAXBUFSIZE      1024

#if PARSE_CTREES_MAX_COLNAME_LEN < 64
#error Some of the Consistent-Trees column names are long. Please increase PARSE_CTREES_MAX_COLNAME_LEN to be at least 64
#endif

/* Function-like macros */
#ifdef NDEBUG
#define PARSE_CTREES_XASSERT(EXP, EXIT_STATUS, ...)                                  \
    do {                                                                \
    } while (0)
#else
#define PARSE_CTREES_XASSERT(EXP, EXIT_STATUS, ...)                                  \
    do {                                                                \
        if (!(EXP)) {                                                   \
            fprintf(stderr, "Error in file: %s\tfunc: %s\tline: %d with expression `" #EXP "'\n", \
                    __FILE__, __FUNCTION__, __LINE__);                  \
            fprintf(stderr, __VA_ARGS__);                               \
            return EXIT_STATUS;                                         \
        }                                                               \
    } while (0)
#endif




enum parse_numeric_types
{
    I32 = 0, /* int32_t */
    I64 = 1, /* int64_t */
    U32 = 2, /* uint32_t */
    U64 = 3, /* uint64_t */
    F32 = 4, /* float */
    F64 = 5, /* double */
    num_numeric_types
};


struct base_ptr_info {
    int64_t num_base_ptrs;
    void **base_ptrs[PARSE_CTREES_MAX_NCOLS];/* because the pointers may need to be re-allocated, I need 'void **' */
    size_t base_element_size[PARSE_CTREES_MAX_NCOLS];/* sizeof(**(base_ptrs[i])) --> in bytes */

    union {
        int64_t N;/* number of rows read */
        int64_t nhalos; /* for convenience */
        int64_t nhalos_read;
    };
    union {
        /* to remove any confusion about what nallocated refers to
           --> nallocated is the number of elements allocated for each
           element in base_ptrs (i.e., alloc'ed/re-alloc'ed via the void **base_ptrs)
         */
        int64_t nhalos_allocated;/* number of elements allocated in each `base_ptr` */
        int64_t nallocated;
    };
};



/* By storing the struct elements on the stack,
   I avoid pesky issues of malloc/free */
struct ctrees_column_to_ptr {
    int64_t ncols;/* number of columns requested */
    int32_t column_number[PARSE_CTREES_MAX_NCOLS];/* column number in CTREES data */
    enum parse_numeric_types field_types[PARSE_CTREES_MAX_NCOLS];/* destination data-type, i.e, how to parse the string into a valid numeric value */
    
    /* For array-of-structure type base-ptrs, this is the offsetof(field);
       for structure of arrays, this should be 0
       
       this offset must be >= 0 and < size of each element of the base ptr
       For offset values that are not 0, absolutely use the OFFSETOF macro
       to derive the byte offset of each field */
    size_t dest_offset_to_element[PARSE_CTREES_MAX_NCOLS];/* in bytes */
    int64_t base_ptr_idx[PARSE_CTREES_MAX_NCOLS];/* index into the base_ptr array within base_ptr_info struct */
};


static inline int * match_column_name(const char (*wanted_columns)[PARSE_CTREES_MAX_COLNAME_LEN], const int nwanted, const char (*names)[PARSE_CTREES_MAX_COLNAME_LEN], const int totncols)
{
    int *columns = calloc(nwanted, sizeof(*columns));
    PARSE_CTREES_XASSERT( columns != NULL, NULL,
                          "Error: Could not allocate memory for reading in the columns for each of the %d fields\n",
                          nwanted);
    for(int i=0;i<nwanted;i++) {
        columns[i] = -1;
    }
    int nfound=0;
    for(int i=0;i<nwanted;i++) {
        const char *wanted_colname = wanted_columns[i];
        int found=0;
        for(int j=0;j<totncols;j++) {
            const char *file_colname = names[j];
            if (strcasecmp(wanted_colname, file_colname) == 0) {
                /* fprintf(stderr, "Found `%s` in column # %d as with name `%s`\n", wanted_colname, j, file_colname); */
                columns[i] = j;
                nfound++;
                found = 1;
                break;
            }
        }
        if(found == 0) {
            fprintf(stderr,"Did not find requested column `%s'\n", wanted_colname);
        }
    }
    /* fprintf(stderr,"Found %d columns out of the requested %d\n", nfound, nwanted); */
    return columns;
}


static inline int reallocate_base_ptrs(struct base_ptr_info *base_info, const int64_t new_N)
{
    for(int64_t i=0;i<base_info->num_base_ptrs;i++) {
        void **this_ptr = base_info->base_ptrs[i];
        const size_t size = base_info->base_element_size[i];
        void *tmp = realloc(*this_ptr, size*new_N);
        if(tmp == NULL) {
            fprintf(stderr,"Error: Failed to re-allocated memory to go from %"PRId64" to %"PRId64" elements, each of size = %zu bytes\n",
                    base_info->nallocated, new_N, size);
            perror(NULL);
            return EXIT_FAILURE;
        }

        /* we have successfully re-allocted => assign the new pointer address */
        *(base_info->base_ptrs[i]) = tmp;
    }
    base_info->nallocated = new_N;
    return EXIT_SUCCESS;
}


static inline int parse_header_ctrees(char (*column_names)[PARSE_CTREES_MAX_COLNAME_LEN], enum parse_numeric_types *field_types,
                                      int64_t *base_ptr_idx, size_t *dest_offset_to_element,
                                      const int64_t nfields, const char *filename, struct ctrees_column_to_ptr *column_info)
{
    /* Because the struct elements (of column_info) are stored on the stack,
       need to check that nfields can fit */
    if(nfields > PARSE_CTREES_MAX_NCOLS) {
        fprintf(stderr,"Error: You have requested %"PRId64" columns but there is only space to store %"PRId64"\n",nfields, (int64_t) PARSE_CTREES_MAX_NCOLS);
        fprintf(stderr,"Please define the macro variable `PARSE_CTREES_MAX_NCOLS' to be larger than %"PRId64" (before including the file `%s')\n",
                nfields, __FILE__);
                
        return EXIT_FAILURE;
    } 


    FILE *fp = fopen(filename, "rt");
    if(fp == NULL) {
        fprintf(stderr,"Error: Could not open file `%s'\n",filename);
        perror(NULL);
        return EXIT_FAILURE;
    }

    char linebuf[PARSE_CTREES_MAXBUFSIZE];
    if(fgets(linebuf, PARSE_CTREES_MAXBUFSIZE, fp) != NULL) {

        /* only need the first line -> close the file */
        fclose(fp);

        /* first check that the first character is a '#' */
        if(linebuf[0] != '#') {
            fprintf(stderr,"Error: Consistent-Trees output always contain '#' as the comment character\n"
                    "However, the first character in the buffer is '%c'\nEntire line is `%s'", linebuf[0], linebuf);
            return EXIT_FAILURE;
        }
        char *tofree, *string;
        
        tofree = string = strdup(linebuf);
        PARSE_CTREES_XASSERT(string != NULL, EXIT_FAILURE,
                             "Error: Could not duplicate the header line (header: = `%s`\n)",
                             linebuf);
        
        int totncols = 0;
        char *token = NULL;
        
        /* consistent-trees currently uses white-space */
        while ((token = strsep(&string, " ,")) != NULL) {
            /* fprintf(stderr,"%35s %zu\n", token, strlen(token)); */
            totncols++;
        }
        free(tofree);


        /* read succeeded -> now parse the column names */
        const char delimiters[] = " ,\n#";/* space, comma, new-line, and #*/
        char (*names)[PARSE_CTREES_MAX_COLNAME_LEN] = calloc(totncols, sizeof(*names));
        PARSE_CTREES_XASSERT(names != NULL, EXIT_FAILURE,
                             "Error: Could not allocate memory to store each column name (total size requested = %zu bytes\n)",
                             totncols * sizeof(*names));
        
        tofree = string = strdup(linebuf);
        PARSE_CTREES_XASSERT(string != NULL, EXIT_FAILURE,
                             "Error: Could not duplicate the header line (header: = `%s`\n)",
                             linebuf);
        
        int col = 0;
        while ((token = strsep(&string, delimiters)) != NULL) {
            size_t size=0, totlen = strlen(token);
            if(totlen == 0) continue;
            /* fprintf(stderr,"[%d] -- '%s' -- ", col, token); */
            PARSE_CTREES_XASSERT(totlen > 0 && totlen < PARSE_CTREES_MAX_COLNAME_LEN, EXIT_FAILURE,
                                 "totlen = %zu should be between (0, %d)\n",
                                 totlen, (int) PARSE_CTREES_MAX_COLNAME_LEN);
            char *colname = names[col];
            for(size_t i=0;i<totlen;i++) {
                /* if(token[i] == '#') continue; */
                if(token[i] == '(') {

#if 1
                    /* locate the ending ')' -- this while loop is only for additional
                       testing and can be commented out */
                    size_t j = i+1;
                    while(j < totlen) {
                        if(token[j] == ')') {
                            token[j] = '\0';
                            /* fprintf(stderr," `token = %s` ", &token[i+1]); */
                            int ctrees_colnum = atoi(&(token[i+1]));
                            PARSE_CTREES_XASSERT(ctrees_colnum == col, EXIT_FAILURE,
                                                 "ctrees_colnum = %d should equal col = %d\n",
                                                 ctrees_colnum, col);
                            break;
                        }
                        j++;
                    }
#endif
                    break;
                }
                colname[size] = token[i];
                size++;
            }
            colname[size] = '\0';
            /* fprintf(stderr, " `%s` \n", names[col]); */
            col++;
        }
        PARSE_CTREES_XASSERT(col == totncols, EXIT_FAILURE,
                             "Error: Previous parsing indicated %d columns in the header but only found %d actual column names\n"
                             "Please check that the delimiters spefied to `strsep` are the same in all calls\n",
                             totncols, col);
        free(tofree);

        int * matched_columns = match_column_name(column_names, nfields, names, totncols);
        if(matched_columns == NULL) {
            return EXIT_FAILURE;
        }
        
        /* do not need the actual names of every column in the ctrees file any longer -> free that memory */
        free(names);

        
        /* now sort the matched columns */
#define SGLIB_CHAR_ARRAY_ELEMENTS_EXCHANGER(maxlen, a, i, j) {char _sgl_aee_tmp_[maxlen]; memmove(_sgl_aee_tmp_, (a)[(i)], maxlen);memmove((a)[(i)], (a)[(j)], maxlen); memmove((a)[(j)], _sgl_aee_tmp_, maxlen);}
        
        /* sort the matched_columns in ascending order */
#define MULTIPLE_ARRAY_EXCHANGER(type,a,i,j) {                          \
            SGLIB_ARRAY_ELEMENTS_EXCHANGER(enum parse_numeric_types, field_types,i,j); \
            SGLIB_ARRAY_ELEMENTS_EXCHANGER(int, matched_columns, i, j); \
            SGLIB_ARRAY_ELEMENTS_EXCHANGER(int64_t, base_ptr_idx, i, j); \
            SGLIB_ARRAY_ELEMENTS_EXCHANGER(size_t, dest_offset_to_element, i, j); \
            SGLIB_CHAR_ARRAY_ELEMENTS_EXCHANGER(PARSE_CTREES_MAX_COLNAME_LEN, column_names, i, j); \
        }
        
        SGLIB_ARRAY_QUICK_SORT(int, matched_columns, nfields, SGLIB_NUMERIC_COMPARATOR , MULTIPLE_ARRAY_EXCHANGER);
#undef SGLIB_CHAR_ARRAY_ELEMENTS_EXCHANGER
#undef MULTIPLE_ARRAY_EXCHANGER


        /* now assign only the columns that were found
           into the column_info struct */
        column_info->ncols = 0;
        for(int i=0;i<nfields;i++) {
            if(matched_columns[i] == -1) continue;

            const int icol = column_info->ncols;
            column_info->column_number[icol] = matched_columns[i];
            column_info->field_types[icol] = field_types[i];
            column_info->dest_offset_to_element[icol] = dest_offset_to_element[i];
            column_info->base_ptr_idx[icol] = base_ptr_idx[i];
            column_info->ncols++;
        }
        free(matched_columns);
    } else {
        fprintf(stderr,"Error: Could not read the first line (the header) in the file `%s'\n", filename);
        perror(NULL);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static inline int parse_line_ctrees(const char *linebuf, const struct ctrees_column_to_ptr *column_info, struct base_ptr_info *base_ptr_info)
{
    if(base_ptr_info->nallocated == base_ptr_info->N ) {
        const int64_t new_N = 2*base_ptr_info->N;
        int status = reallocate_base_ptrs(base_ptr_info, new_N);
        if(status != EXIT_SUCCESS) return status;
    }

    int icol = -1;
    char *tofree, *string;
    tofree = string = strdup(linebuf);
    char *token = NULL;
    for(int i=0;i<column_info->ncols;i++) {
        const int wanted_col = column_info->column_number[i];
        const int64_t base_ptr_idx = column_info->base_ptr_idx[i];
        PARSE_CTREES_XASSERT(base_ptr_idx < base_ptr_info->num_base_ptrs, EXIT_FAILURE,
                             "Error: Valid values for base pointer index must be in range [0, %"PRId64"). Got %"PRId64" instead\n",
                             base_ptr_info->num_base_ptrs, base_ptr_idx);
        char *dest = *((char **) (base_ptr_info->base_ptrs[base_ptr_idx]));
        const size_t base_ptr_stride = base_ptr_info->base_element_size[base_ptr_idx];
        const size_t dest_offset = column_info->dest_offset_to_element[i];
        PARSE_CTREES_XASSERT(base_ptr_stride >= 4, EXIT_FAILURE,
                             "Error: Stride=%zu is expected in bytes with a minimum of 4 bytes since that's "
                             "the smallest data-type supported (corresponding to float or int32_t).\n"
                             "Perhaps you forgot to multiply by the sizeof(element)?\n",
                             base_ptr_stride);
        PARSE_CTREES_XASSERT(dest_offset <= base_ptr_stride, EXIT_FAILURE,
                             "Error: The offset from the starting address of an element can be at most the total stride in bytes\n"
                             "In this case offset=%zu must in the closed range [0, %zu]. Perhaps you mis-typed the offset?\n",
                             dest_offset, base_ptr_stride);
            
        /* get to the starting offset for this N'th element */
        dest += base_ptr_info->N * base_ptr_stride;

        /* now get to this particular field */
        dest += dest_offset;
        const enum parse_numeric_types wanted_type = column_info->field_types[i];/* this is the type for the destination (hence called 'field_types' rather than 'column_types') */

        /* there might be duplicate column numbers in matched_columns
           then the following while loop should immediately exit (without
           executing any lines within)
           and we will re-use the previous parsed value of token */
        while(icol < wanted_col) {
            do {
                token = strsep(&string, " ");
                PARSE_CTREES_XASSERT(token != NULL, EXIT_FAILURE,
                                     "Error: token=`%s` should not be NULL\n",
                                     token);
                /* fprintf(stderr,"token = '%s' strlen(token) = %zu\n", token, strlen(token)); */
            } while(token != NULL && ((token[0] == '\0') || (token[0] == ' ')));
            icol++;
        }
        PARSE_CTREES_XASSERT(token != NULL && token[0] != '\0', EXIT_FAILURE,
                             "Error: token=`%s` should have valid non-zero numeric value at this stage.\n",
                             token);
        /* fprintf(stderr,"token = `%s` icol = %d name = %s --  ", token, icol, names[icol]); */

        switch(wanted_type) {
        case F32:{
            float tmp = strtof(token, NULL);
            /* fprintf(stderr,"[float] := %f\n", tmp); */
            *((float *) dest) = tmp;
            break;
        }
        case F64:{
            double tmp = strtod(token, NULL);
            /* fprintf(stderr,"[double] := %lf\n", tmp); */
            *((double *) dest) = tmp;
            break;
        }
        case I32:{
            int32_t tmp = (int32_t) strtol(token, NULL, 10);
            /* fprintf(stderr,"[int32_t] := %"PRId32"\n", tmp); */
            *((int32_t *) dest) = tmp;
            break;
        }
        case I64:{
            int64_t tmp = (int64_t) strtoll(token, NULL, 10);
            /* fprintf(stderr,"[int64_t] := %"PRId64"\n", tmp); */
            *((int64_t *) dest) = tmp;
            break;
        }
        default:
            fprintf(stderr,"Error: Unknown value for parse type = %d\n", wanted_type);
            fprintf(stderr,"Known values are in the range : [%d, %d)\n", I32, num_numeric_types);
            return EXIT_FAILURE;
        }
        base_ptr_info->N++;
    }
    free(tofree);

    return EXIT_SUCCESS;
}


static inline int read_single_tree_ctrees(int fd, off_t offset, const struct ctrees_column_to_ptr *column_info, struct base_ptr_info *base_ptr_info)
{
    /* Because the struct elements (of column_info) are stored on the stack,
       need to check that nfields can fit */
    if(column_info->ncols  > PARSE_CTREES_MAX_NCOLS) {
        fprintf(stderr,"Error: You have requested %"PRId64" columns but there is only space to store %"PRId64"\n",
                column_info->ncols, (int64_t) PARSE_CTREES_MAX_NCOLS);
        fprintf(stderr,"Please define the macro variable `PARSE_CTREES_MAX_NCOLS' to be larger than %"PRId64" (before including the file `%s')\n",
                column_info->ncols, __FILE__);
                
        return EXIT_FAILURE;
    } 

    const int num_chars_first_tree_line = 30;
    char first_tree_line[num_chars_first_tree_line];
    if(pread(fd, &first_tree_line, num_chars_first_tree_line, offset) > 0) {
        for(int i=0;i<num_chars_first_tree_line;i++) {
            offset++;
            if(first_tree_line[i] == '\n') {
                break;
            }
        }
    } else {
        return EXIT_FAILURE;
    }
    
    char read_buffer[4*PARSE_CTREES_MAXBUFSIZE];
    const size_t to_read_bytes = sizeof(read_buffer)/sizeof(read_buffer[0]) - 1;
    int done = 0;
    /* two things can happen while reading -> EOF or I reach the next tree */
    while(done == 0) {
        ssize_t nbytes_read = pread(fd, read_buffer, to_read_bytes, offset);
        if(nbytes_read == 0) {
            done = 1;
        } else if(nbytes_read < 0) {
            fprintf(stderr,"Error: trying to read %zu bytes from file failed. Encountered negative bytes read \n", to_read_bytes);
            perror(NULL);
            return EXIT_FAILURE;
        } else {
            /* some bytes were read -> now let's parse one line at a time */
            ssize_t bytes_processed = 0;
            char *start = &(read_buffer[0]);
            int keep_parsing = 1;
            while(keep_parsing == 1) {
                ssize_t curr_pos = 0;
                char *this = start;
                while(curr_pos < nbytes_read && *this != '\n') {
                    if(*this == '#') {
                        /* we have encountered the beginning of a new tree (new line and begins with '#tree ')*/
                        done = 1;
                        curr_pos = 0;/* should be 0 anyway but ensuring that bytes_processed does not get incremented */
                        keep_parsing = 0;
                        break;
                    }
                    curr_pos++;this++;
                }
                if(curr_pos == nbytes_read) {
                    keep_parsing = 0;
                    curr_pos = 0;
                    break;
                }
                
                read_buffer[curr_pos] = '\0';
                start += curr_pos + 1;/* might point beyond valid memory but should not get de-referenced */
                bytes_processed += curr_pos + 1;/* not sure if I need this +1 : MS 2nd Aug, 2018*/
                
                char linebuf[PARSE_CTREES_MAXBUFSIZE];
                memmove(linebuf, read_buffer, curr_pos);
                int status = parse_line_ctrees(linebuf, column_info, base_ptr_info);
                if(status != EXIT_SUCCESS) {
                    return status; 
                }
            }
            PARSE_CTREES_XASSERT(bytes_processed <= nbytes_read, EXIT_FAILURE,
                                 "Error: bytes processed = %zd should be at most num bytes read = %zd\n",
                                 bytes_processed, nbytes_read);
            offset += bytes_processed;
        }
    }

    return EXIT_SUCCESS;
}

/* these two macros are for internal use only
   and can therefor be undefined */
#undef PARSE_CTREES_MAXBUFSIZE
#undef PARSE_CTREES_XASSERT


#if 0
/* this will be required externally to pass in the
   array of strings, containing the column names  */
#undef PARSE_CTREES_MAX_COLNAME_LEN
#endif

