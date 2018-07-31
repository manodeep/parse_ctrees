# parse_ctrees
Header-only C code to robustly parse Consistent-Trees output

# Why this code exists
I have frequently needed to parse Consistent-Trees files in C and always have
to count up the column numbers by hand (for each simulation) and make sure I am
reading the correct columns (in my `ConvertCtrees` code). Multiple times I have 
messed up the counting and produced incorrect results downstream, requiring
another conversion process. 

I am planning to solve this issue once and for all. 

# Prior Work
Yao-Yuan Mao has an excellent python reader for Consistent-Trees [here](https://bitbucket.org/yymao/helpers/src/master/)

# Code Design
In the general case, any column from the Consistent-Trees output (i.e.,
something like ``tree_?_?_?.dat``) can be assigned to an arbitrary pointer. 
Every requested column has a column number, column type, a destination base
pointer, size of each element of the destination base pointer, and an offset in
bytes to reach the field (only relevant for compound types like ``struct`` or
``unions``). 

