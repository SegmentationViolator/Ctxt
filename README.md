# About
I worked on this project because I wanted to learn C and do a fun project.
This editor is not mean't for actual use.
**(you can use this text editor if you have plenty of time to waste and want to waste that time)**

This project is based on this booklet:
[Build Your Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/index.html).

The booklet is written by [Snaptoken](https://github.com/snaptoken).

> Compiled binaries aren't provided so if you want to run this on your machine (I dont know why someone would do that) you will have to compile them yourself.

# Compile Instructions
## Pre-requisites
- C compiler. (e.g. GCC, Clang)
- (Optional) git.

## compilation
keep `main.c` `ini.c` and `ini.h` in a single directory.

**OR**

```
git clone https://github.com/Sakon13/ctxt
cd ctxt
```

```
$(CC) -std=c99 -Wall -Wextra -pedantic -o ctxt -lm
```
> replace $(CC) with the C compiler you are using.

run `./ctxt`

# Contribute
If you encounter any bugs while trying out the editor please report them.

# Changelog
check out [changelog.txt](https://github.com/Sakon13/ctxt/blob/main/changelog.txt)
