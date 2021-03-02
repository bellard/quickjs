
(function()
  -- generate "quickjs-version.h" using VERSION file
  local file = io.open("VERSION", "r")
  local vers = file:read()
  file:close()
  vars = vers:gsub("%s+", "")
  file = io.open("quickjs-version.h", "w+")
  file:write("#define QUICKJS_VERSION \"" .. vers .. "\"")
  file:close()
end)()  


newoption {
   trigger     = "jsx",
   description = "Will add JSX support"
}

newoption {
   trigger     = "storage",
   description = "Will add persistent storage support"
}

workspace "quickjs"
	-- Premake output folder
	location(path.join(".build", _ACTION))

  defines {
  	  "JS_STRICT_NAN_BOXING", -- this option enables x64 build on Windows/MSVC
      "CONFIG_BIGNUM"
    } 

  if _OPTIONS["jsx"] then 
    defines { "CONFIG_JSX" } -- native JSX support - enables JSX literals
  end

  if _OPTIONS["storage"] then 
    defines { "CONFIG_STORAGE" } -- persistent storage support
  end


	platforms { "x86", "x64", "arm32", "arm64"  } 

	-- Configuration settings
	configurations { "Debug", "Release" }

	filter "platforms:x86"
  	architecture "x86"
	filter "platforms:x64"
  	architecture "x86_64"  
	filter "platforms:arm32"
  	architecture "ARM"  
	filter "platforms:arm64"
  	architecture "ARM64"  

	filter "system:windows"
  	removeplatforms { "arm32" }  

	-- Debug configuration
	filter { "configurations:Debug" }
		defines { "DEBUG" }
		symbols "On"
		optimize "Off"

	-- Release configuration
	filter { "configurations:Release" }
		defines { "NDEBUG" }
		optimize "Speed"
		inlining "Auto"

	filter { "language:not C#" }
		defines { "_CRT_SECURE_NO_WARNINGS" }
		buildoptions { "/std:c++latest" }
		systemversion "latest"

	filter { }
		targetdir ".bin/%{cfg.longname}/"
		exceptionhandling "Off"
		rtti "Off"
		--vectorextensions "AVX2"

-----------------------------------------------------------------------------------------------------------------------

project "quickjs"
	language "C"
	kind "StaticLib"
	files {
    "cutils.h",
		"cutils.c",
		"libregexp.c",
		"libunicode.c",
		"quickjs.c",
		"quickjs-libc.c",
		"libbf.c",
		"libregexp.h",
		"libregexp-opcode.h",
		"libunicode.h",
		"libunicode-table.h",
		"list.h",
		"quickjs.h",
		"quickjs-atom.h",
		"quickjs-libc.h",
		"quickjs-opcode.h",
		"quickjs-jsx.h",
	}

if _OPTIONS["storage"] then 
  exceptionhandling "On"
  files {
    "storage/quickjs-storage.c",
    "storage/quickjs-storage.h",
    "storage/dybase/src/*.cpp",
    "storage/dybase/src/*.h",
    "storage/dybase/include/*.h"
  }
  includedirs {
    "storage/dybase/include"
  }
end

-----------------------------------------------------------------------------------------------------------------------

project "qjsc"
	language "C"
	kind "ConsoleApp"
	links { "quickjs" }
	files {
		"qjsc.c"
	}

-----------------------------------------------------------------------------------------------------------------------

project "qjs"
	language "C"
	kind "ConsoleApp"
	links { "quickjs" }
	dependson { "qjsc" }
	files {
		"qjs.c",
		"repl.js",
		"repl.c",
		"qjscalc.js",
		"qjscalc.c"
	}

-- Compile repl.js and save bytecode into repl.c
prebuildcommands { "\"%{cfg.buildtarget.directory}/qjsc.exe\" -c -o \"../../repl.c\" -m \"../../repl.js\"" }
prebuildcommands { "\"%{cfg.buildtarget.directory}/qjsc.exe\" -c -o \"../../qjscalc.c\" -m \"../../qjscalc.js\"" }
