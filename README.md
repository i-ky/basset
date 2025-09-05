# basset [![Open in GitHub Codespaces](https://github.com/codespaces/badge.svg)](https://codespaces.new/i-ky/basset)

A tool that generates a
[compilation database](https://clang.llvm.org/docs/JSONCompilationDatabase.html)
for [clang] tooling using [ptrace] and [procfs].

## summary

If you are reading this
you probably know what compilation database is
and what it can be used for.
And you are probably unlucky to use a build system
that can't generate `compile_commands.json` for you.
Your build system may even be especially non-cooperative
defeating tools like [bear], [clade] and [compiledb].
For example,
build system may not respect injected `CC` and `CXX` environment variables,
may hard-code compiler paths,
may use statically linked binaries rendering `LD_PRELOAD` tricks useless
or may not pass `make` flags recursively making build logs incomplete.
There is however a less known [compile-db-gen],
which uses [strace] to capture compiler invocations
and should work even in these conditions.

This project aims to improve on this idea:
- to reduce overhead [ptrace] is used directly instead of [strace];
- working directory, executable path and arguments are read from [procfs] reducing code complexity.

## disclaimer

It has been developed and tested on Linux.
Whether it works on other systems is an open question.
Your feedback (both positive and negative) is highly appreciated!

## prerequisites

You will need `make` and C++ compiler.

## compile

Simply:
```bash
make
```
It should produce `basset` executable in the project root directory.

## install

Copy `basset` executable to a desired location or create a symlink.

## use

A typical use would be:
```
basset -- <your-build-command>
```
Extra options can precede `--` if needed.

[bear]: https://github.com/rizsotto/Bear
[clade]: https://github.com/17451k/clade
[clang]: https://clang.llvm.org
[compile-db-gen]: https://github.com/sunlin7/compile-db-gen
[compiledb]: https://github.com/nickdiego/compiledb
[procfs]: https://en.wikipedia.org/wiki/Procfs
[ptrace]: https://en.wikipedia.org/wiki/Ptrace
[strace]: https://strace.io
