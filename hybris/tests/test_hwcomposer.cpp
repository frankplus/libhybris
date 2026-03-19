/*
 * Copyright (c) 2012 Carsten Munk <carsten.munk@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <android-config.h>

#if ((ANDROID_VERSION_MAJOR >= 4 && ANDROID_VERSION_MINOR >= 2) || ANDROID_VERSION_MAJOR >= 5)

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>
#include <pthread.h>
#include <dlfcn.h>

/* android_dlopen: hybris Android linker's dlopen, searches HYBRIS_LD_LIBRARY_PATH */
extern "C" void *android_dlopen(const char *filename, int flag);

#include "test_common.h"

const char vertex_src [] =
"                                        \
   attribute vec4        position;       \
   varying mediump vec2  pos;            \
   uniform vec4          offset;         \
                                         \
   void main()                           \
   {                                     \
      gl_Position = position + offset;   \
      pos = position.xy;                 \
   }                                     \
";


const char fragment_src [] =
"                                                      \
   varying mediump vec2    pos;                        \
   uniform mediump float   phase;                      \
                                                       \
   void  main()                                        \
   {                                                   \
      gl_FragColor  =  vec4( 1., 0.9, 0.7, 1.0 ) *     \
        cos( 30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y)   \
             + atan(pos.y,pos.x) - phase );            \
   }                                                   \
";


GLfloat norm_x    =  0.0;
GLfloat norm_y    =  0.0;
GLfloat offset_x  =  0.0;
GLfloat offset_y  =  0.0;
GLfloat p1_pos_x  =  0.0;
GLfloat p1_pos_y  =  0.0;

GLint phase_loc;
GLint offset_loc;
GLint position_loc;

const float vertexArray[] = {
	0.0,  1.0,  0.0,
	-1.,  0.0,  0.0,
	0.0, -1.0,  0.0,
	1.,  0.0,  0.0,
	0.0,  1.,  0.0
};

int main(int argc, char **argv)
{
	/* Pre-load gralloc mapper passthrough library via android_dlopen so it
	 * is already in the hybris linker's table when libhidlbase's
	 * android_load_sphal_library tries to load it. Without this, the SPHAL
	 * loader fails to find the library and GraphicBufferMapper aborts with
	 * "gralloc-mapper is missing". */
	static const char *gralloc_mapper_paths[] = {
		"/android/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so",
		"/android/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl.so",
		NULL
	};
	for (int i = 0; gralloc_mapper_paths[i]; i++) {
		void *h = android_dlopen(gralloc_mapper_paths[i], 1 /* RTLD_LAZY */ | 0x100 /* RTLD_GLOBAL */);
		if (h) break;
	}

	EGLDisplay display;
	EGLConfig ecfg;
	EGLint num_config;
	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 32,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLSurface surface;
	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLContext context;

	EGLBoolean rv;

	HWComposer *win = create_hwcomposer_window();

	display = eglGetDisplay(NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(display != EGL_NO_DISPLAY);

	rv = eglInitialize(display, 0, 0);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	eglChooseConfig((EGLDisplay) display, attr, &ecfg, 1, &num_config);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);


	surface = eglCreateWindowSurface((EGLDisplay) display, ecfg, (EGLNativeWindowType) static_cast<ANativeWindow *> (win), NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(surface != EGL_NO_SURFACE);

	context = eglCreateContext((EGLDisplay) display, ecfg, EGL_NO_CONTEXT, ctxattr);
	assert(eglGetError() == EGL_SUCCESS);
	assert(context != EGL_NO_CONTEXT);

	/* eglMakeCurrent must be called explicitly — not via assert — because the
	 * OHOS release build defines NDEBUG, which silently elides assert expressions
	 * and would leave no current context, causing all subsequent GL calls to fail. */
	EGLBoolean mc = eglMakeCurrent((EGLDisplay) display, surface, surface, context);
	assert(mc == EGL_TRUE);

	/* Use eglGetProcAddress to get GL functions directly from the vendor EGL,
	 * bypassing the libGLESv2.z.so dispatch layer which has bionic TLS issues
	 * when mixing OHOS musl and Android bionic TLS slots. */
	typedef const GLubyte* (*PFNGLGETSTRINGPROC_t)(GLenum);
	PFNGLGETSTRINGPROC_t my_glGetString =
		(PFNGLGETSTRINGPROC_t)eglGetProcAddress("glGetString");

	const char *version = my_glGetString ? (const char *)my_glGetString(GL_VERSION) : NULL;
	if (version) {
		printf("%s\n", version);
	}

	GLuint shaderProgram = create_program(vertex_src, fragment_src);
	glUseProgram  ( shaderProgram );    // and select it for usage

	//// now get the locations (kind of handle) of the shaders variables
	position_loc  = glGetAttribLocation  ( shaderProgram , "position" );
	phase_loc     = glGetUniformLocation ( shaderProgram , "phase"    );
	offset_loc    = glGetUniformLocation ( shaderProgram , "offset"   );
	if ( position_loc < 0  ||  phase_loc < 0  ||  offset_loc < 0 ) {
		return 1;
	}

	//glViewport ( 0 , 0 , 800, 600); // commented out so it uses the initial window dimensions
	glClearColor ( 1. , 1. , 1. , 1.);    // background color
	float phase = 0;
	int i;
	for (i=0; i<1020*60; ++i) {
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f ( phase_loc , phase );  // write the value of phase to the shaders phase
		phase  =  fmodf ( phase + 0.5f , 2.f * 3.141f );    // and update the local variable

		glUniform4f ( offset_loc  ,  offset_x , offset_y , 0.0 , 0.0 );

		glVertexAttribPointer ( position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertexArray );
		glEnableVertexAttribArray ( position_loc );
		glDrawArrays ( GL_TRIANGLE_STRIP, 0, 5 );

		eglSwapBuffers ( (EGLDisplay) display, surface );  // get the rendered buffer to the screen
	}

	printf("stop\n");

#if 0
	(*egldestroycontext)((EGLDisplay) display, context);
	printf("destroyed context\n");

	(*egldestroysurface)((EGLDisplay) display, surface);
	printf("destroyed surface\n");
	(*eglterminate)((EGLDisplay) display);
	printf("terminated\n");
	android_dlclose(baz);
#endif
	return 0;
}

#else
#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("test_hwcomposer is not supported in this build\n");
    return 0;
}
#endif

// vim:ts=4:sw=4:noexpandtab
