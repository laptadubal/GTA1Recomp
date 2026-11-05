// Translation unit kept so that platform-specific GL loading code can live
// here later (GLEW/GLAD on Windows).  On macOS and Linux the GL symbols are
// provided directly by the linked OpenGL library.

#include "graphics/gl.h"
