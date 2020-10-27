

# QuickJS Javascript Engine 

Authors: Fabrice Bellard and Charlie Gordon

Ported from https://bellard.org/quickjs/ and its official GitHub mirror https://github.com/bellard/quickjs

By Andrew Fedoniouk (a.k.a. c-smile)

This version is 

* Microsoft Visual C++ compatible/compileable
* Is used in Sciter.JS

The main documentation is in doc/quickjs.pdf or doc/quickjs.html.

# Build using Microsoft Visual Studio (2017 or 2019)

Prerequisite: **premake5** - [download](https://premake.github.io/download.html) and install it.

Then go to /win folder and run premake-vs2017.bat or premake-vs2019.bat . 

It will generate .build/vs2017/quickjs-msvc.sln and open it in Microsoft Visual Studio.

Press F5 to compile it and run qjs - interactive JS command line application.

Check (wiki)[https://github.com/c-smile/quickjspp/wiki]


