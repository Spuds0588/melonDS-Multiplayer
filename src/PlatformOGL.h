#ifndef PLATFORMOGL_H
#define PLATFORMOGL_H

// If you don't wanna use glad for your platform,
// define MELONDS_GL_HEADER to the path of some other header
// that pulls in the necessary OpenGL declarations.
// The value is a string literal, so it carries no quotes or angle brackets of
// its own; a relative path resolves against this file's directory. The default
// points at the glad copy in this tree, which the OpenGL renderer also compiles
// (src/CMakeLists.txt) since the Qt/SDL frontend that used to own it was retired.
// The old default was "\"frontend/glad/glad.h\"", whose inner quotes became part
// of the filename: GCC reports `\"frontend/glad/glad.h\"` missing even though the
// file is right there. That default went unnoticed because the Qt/SDL target
// defined MELONDS_GL_HEADER itself; with that frontend retired, it is the one
// this tree actually uses.

#ifndef MELONDS_GL_HEADER
#define MELONDS_GL_HEADER "frontend/glad/glad.h"
#endif

#include MELONDS_GL_HEADER

#endif
