# About
I worked on this project because I wanted to learn C and do a fun project.
> This editor is written and tested to be run only on linux. you may try to run it on other OSes but it's not guaranteed to work on other OSes

This project is based on this booklet:
[Build Your Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/index.html).

The booklet is written by [Snaptoken](https://github.com/snaptoken).

> Compiled binaries aren't provided so if you want to run this on your machine you will have to compile it yourself.

# Compilation Instructions
## Pre-requisites
- C compiler. (e.g. GCC, Clang)
- (Optional) git.

## compilation

```
git clone https://github.com/SEG-V/ctxt
cd ctxt
make build
./ctxt
```

# Configuration
The configuration file `config.ini` should be loacted at `$HOME/.config/ctxt/`

| options    | value        | default | description       |
| ---------- | ------------ | ------- | ----------------- |
| tabstop    | unsigned int | 8       | width of '\t'     |
| numberline | on | off     | off     | toggle numberline |

# Contribute
If you encounter any bugs while trying out the editor please report them.

# Changelog
Check out [changelog.md](changelog.md)
