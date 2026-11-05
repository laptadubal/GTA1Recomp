#pragma once

// Platform specific OpenGL includes.
//
// Desktop builds use OpenGL 3.3 core.  Android builds use OpenGL ES 3.0 via
// the GTA_GLES compile definition (set by CMake).  macOS ships a Core Profile
// header with all 3.3 entry points; Linux/BSD use the system GL headers with
// prototypes enabled.  Windows builds should add GLEW/GLAD (see README).

#if defined(GTA_GLES)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif
