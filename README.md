# ShaderAssist
Simple one-file shader utility tool that automatically compiles shaders to SPIRV when modified.

This is meant to be a simple drag'n drop one-file utility tool. It periodically checks all specified shader files in its local or specified directory and re-compiles modified shaders automatically. The tool is meant to save you a lot of back-and-forth work when quickly iterating shaders that require a SPIRV conversion. The tool is configurable through shaderassist.ini. By default, Google's SPIRV compiler is used instead of GLSLLangValidator's version as Google's compiler supports extra features like #include preprocessor support; this is configurable through shaderassist.ini.

The tool is built with cross-platform compatability in mind, but it's not been tested on all compilers/operating-systems. Feel free to submit platform-specific fixes when found.


