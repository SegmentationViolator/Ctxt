# About
I worked on this project because I wanted to learn C and do a fun project.
This editor is not mean't for actual use.

This project is based on this booklet:
[Build Your Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/index.html).

The booklet is written by [Snaptoken](https://github.com/snaptoken).

> Compiled binaries aren't provided so if you want to run this on your machine you will have to compile it yourself.

# Compilation Instructions
## Pre-requisites
- C compiler. (e.g. GCC, Clang)
- (Optional) git.

## compilation
Keep `main.c` `ini.c` and `ini.h` in a single directory.

**OR**

```
git clone https://github.com/Sakon13/ctxt
cd ctxt
```

then,
```
make build
./ctxt
```

# Configuration
Unlike other text editors, the config file (ctxt.ini) needs to be stored in the working directory.

| options    | value        | default | description       |
| ---------- | ------------ | ------- | ----------------- |
| tabstop    | unsigned int | 8       | length of '\t'    |
| numberline | on | off     | off     | toggle numberline |

# Contribute
If you encounter any bugs while trying out the editor please report them.

# Changelog
Check out [changelog.md](https://github.com/Sakon13/ctxt/blob/main/changelog.md)
