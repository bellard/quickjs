

# QuickJS Javascript Engine 

Authors: Fabrice Bellard and Charlie Gordon

Ported from https://bellard.org/quickjs/ and its official GitHub mirror https://github.com/bellard/quickjs

By Andrew Fedoniouk (a.k.a. c-smile)

This version is 

* Microsoft Visual C++ compatible/compileable
* Is used in Sciter.JS
* It contains extras: 
  * [JSX](doc/jsx.md) - built-in [facebook::JSX](https://facebook.github.io/jsx/) support with Sciter specific extras.
  * Built-in [Persistence](storage/doc/README.md) - you can think of it as local MongoDB (NoSQL) DB embedded into the language. Pretty small, adds just 70kb into binary.

The main documentation is in doc/quickjs.pdf or [doc/quickjs.html](doc/quickjs.html).

# Build using Microsoft Visual Studio (2017 or 2019)

Prerequisite: **premake5** - [download](https://premake.github.io/download.html) and install it.

Then go to /win folder and run premake-vs2017.bat or premake-vs2019.bat . 

It will generate .build/vs2017/quickjs-msvc.sln and open it in Microsoft Visual Studio.

Press F5 to compile it and run qjs - interactive JS command line application.

# Premake5 and build on other platforms/compilers/ide  

Supported premake options:

* ```--jsx``` - include JSX support;
* ```--storage``` - include Persistent Storage support;

Supported targets (these are built into [Premake](https://premake.github.io/) itself):

* vs2017 - MS Visual Studio 2017
* vs2019 - MS Visual Studio 2019
* gmake2 - gmake files
* etc...

Few examples of other possible configurations: 
```bat
premake5 vs2019 --jsx --storage
premake5 codeblocks --cc=gcc --jsx --storage
premake5 gmake2 --cc=gcc --jsx --storage
premake5 gmake2 --cc=clang --jsx --storage
premake5 gmake2 --cc=clang --jsx --storage
premake5 xcode4 --jsx --storage
```





