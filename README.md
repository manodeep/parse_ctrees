# parse_ctrees
Header-only C code to robustly parse output from [Consistent-Trees](https://bitbucket.org/pbehroozi/consistent-trees) (mergertree code for cosmological simulations)

# Why this code exists
I have frequently needed to parse Consistent-Trees files in C and always have
to count up the column numbers by hand (for each simulation) and make sure I am
reading the correct columns (in my `ConvertCtrees` code). Multiple times I have 
messed up the counting and produced incorrect results downstream, requiring
another conversion process. 

I am planning to solve this issue once and for all. 

# What the Code can do
The code reads a single tree at a time. For each tree, the code can efficiently do the following:

- Automatically parse the header and then read *all/some* of the columns present in Consistent Trees output
- Assign each column to multiple memory locations
- Handle assignments to both structure-of-arrays and array-of-structures
- Account for changes in column-naming convention in Consistent Trees (e.g., `snap_num` vs `snap_idx`)
- Perform various error-checking to "Do the Right Thing"™

# Code Design
In the general case, any column from the Consistent-Trees output (i.e., something like ``tree_?_?_?.dat``) can be assigned to an arbitrary pointer. Every requested column has a column number, column type, a destination base pointer, size of each element of the destination base pointer, and an offset in bytes to reach the field (only relevant for compound types like ``struct`` or ``unions``). 

# Example Usage

See in action [here](https://github.com/manodeep/lfs_sage/blob/lhvt/src/io/read_tree_consistentrees_ascii.c#L189-L317)

# Prior Work
Yao-Yuan Mao has an excellent python reader for Consistent-Trees [here](https://bitbucket.org/yymao/helpers/src/master/)
