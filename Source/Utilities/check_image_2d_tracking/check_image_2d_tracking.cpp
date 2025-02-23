/*
 *  dispTexData.cpp
 *  artoolkitX
 *
 *  Identifies markers in texture image and generates marker set files.
 *
 *  Run with "--help" parameter to see usage.
 *
 *  artoolkitX is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  artoolkitX is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with artoolkitX.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  As a special exception, the copyright holders of this library give you
 *  permission to link this library with independent modules to produce an
 *  executable, regardless of the license terms of these independent modules, and to
 *  copy and distribute the resulting executable under terms of your choice,
 *  provided that you also meet, for each linked independent module, the terms and
 *  conditions of the license of that module. An independent module is a module
 *  which is neither derived from nor based on this library. If you modify this
 *  library, you may extend this exception to your version of the library, but you
 *  are not obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  Copyright 2018 Realmax, Inc.
 *  Copyright 2015 Daqri, LLC.
 *  Copyright 2007-2015 ARToolworks, Inc.
 *
 *  Author(s): Hirokazu Kato, Philip Lamb
 *
 */

// ============================================================================
//    Includes
// ============================================================================

#ifdef _WIN32
#  include <windows.h>
//#  define snprintf _snprintf
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#  define MAXPATHLEN MAX_PATH
#else
#  include <sys/param.h> // MAXPATHLEN
#endif
#include <stdlib.h>                    // malloc(), free()
#include <ARX/AR/ar.h>
#include <ARX/ARG/arg.h>
#include <ARX/OCVT/PlanarTracker.h>
#include <ARX/ARUtil/image_utils.h>
#include "../../ARX/OCVT/OCVConfig.h"
#include "../../ARX/OCVT/TrackingPointSelector.h"
#include "../../ARX/OCVT/HarrisDetector.h"
#include "../../ARX/OCVT/OCVFeatureDetector.h"
#include <SDL2/SDL.h>
#ifdef __APPLE__
#  include <OpenGL/gl.h>
#elif defined(__linux) || defined(_WIN32)
#  include <GL/gl.h>
#endif
#include <Eden/EdenGLFont.h>
#include <vector>
#include <algorithm>

// ============================================================================
//    Constants
// ============================================================================

#define FONT_SIZE 18.0f

enum {
    E_NO_ERROR = 0,
    E_BAD_PARAMETER = 64,
    E_INPUT_DATA_ERROR = 65,
    E_USER_INPUT_CANCELLED = 66,
    E_BACKGROUND_OPERATION_UNSUPPORTED = 69,
    E_DATA_PROCESSING_ERROR = 70,
    E_UNABLE_TO_DETACH_FROM_CONTROLLING_TERMINAL = 71,
    E_GENERIC_ERROR = 255
};

// ============================================================================
//    Global variables
// ============================================================================

// Preferences.
static int              display_templates = 1;
static int              display_features = 1;
static int              display_bins = 1;
static char            *inputFilePath = NULL;

// Input.
static std::shared_ptr<unsigned char> refImage;
static int refImageX, refImageY;
static std::vector<cv::KeyPoint> _featurePoints;
static std::vector<cv::Point2f> _templatePoints[k_OCVTTemplateMatchingMaxPyrLevel + 1];
static TrackingPointSelector _trackSelection[k_OCVTTemplateMatchingMaxPyrLevel + 1];
static int                      _templatePyrLevel = 0;
static double                   imageZoom = 1.0f;


// Drawing.
// Window and GL context.
static SDL_GLContext gSDLImageContext = NULL;
static int gImageContextWidth = 0;
static int gImageContextHeight = 0;
static SDL_Window* gSDLImageWindow = NULL;
static ARGL_CONTEXT_SETTINGS_REF gArglContextSettings = NULL;
static int gShowHelp = 1;
static int gShowMode = 1;

static char             exitcode = -1;
#define EXIT(c) {exitcode=c;exit(c);}

// ============================================================================
//    Function prototypes.
// ============================================================================

static void loadImage(void);
static void getImageFeature();
static void setTemplatePyrLevel(int templatePyrLevel);
static void quit(int rc);
static float calcZoomToFit(int sourceSizeX, int sourceSizeY, int destSizeX, int destSizeY);
static void reshapeImageWindow(int w, int h);
static void keyboard(SDL_Keycode key);
static void processCommandLineOptions(int argc, char *argv[]);
static void usage(char *com);
static void drawImageView(void);
static void drawBackground(const float width, const float height, const float x, const float y);
static void printHelpKeys(void);
static void printMode(void);

int main(int argc, char *argv[])
{
#ifdef DEBUG
    arLogLevel = AR_LOG_LEVEL_DEBUG;
#endif
    
    // Initialize SDL.
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        ARLOGe("Error: SDL initialisation failed. SDL error: '%s'.\n", SDL_GetError());
        return -1;
    }
    
    // Preferences.
    processCommandLineOptions(argc, argv);
    
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    int screenWidth = dm.w;
    int screenHeight = dm.h;

    loadImage();
    float imageZoom = calcZoomToFit(refImageX, refImageY, screenWidth - 200, screenHeight - 200);

    // Create a window to display the image.
    gSDLImageWindow = SDL_CreateWindow(argv[0],
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  (int)(refImageX * imageZoom), (int)(refImageY * imageZoom),
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
                                  );
    if (!gSDLImageWindow) {
        ARLOGe("Error creating window: %s.\n", SDL_GetError());
        quit(-1);
    }
    
    // Create an OpenGL context to draw into.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); // This is the default.
    SDL_GL_SetSwapInterval(1);
    gSDLImageContext = SDL_GL_CreateContext(gSDLImageWindow);
    if (!gSDLImageContext) {
        ARLOGe("Error creating OpenGL context: %s.\n", SDL_GetError());
        return -1;
    }
    int w, h;
    SDL_GL_GetDrawableSize(gSDLImageWindow, &w, &h);
    reshapeImageWindow(w, h);
    
    EdenGLFontInit(1); // contextsActiveCount=1
    EdenGLFontSetFont(EDEN_GL_FONT_ID_Stroke_Roman);
    EdenGLFontSetSize(FONT_SIZE);
    
    getImageFeature();
    
    // Main loop.
    bool done = false;
    while (!done) {
        
        SDL_Event ev;
        while (SDL_WaitEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                done = true;
                break;
            } else if (ev.type == SDL_WINDOWEVENT) {
                //ARLOGd("Window event %d.\n", ev.window.event);
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED && ev.window.windowID == SDL_GetWindowID(gSDLImageWindow)) {
                    //int32_t w = ev.window.data1;
                    //int32_t h = ev.window.data2;
                    int w, h;
                    SDL_GL_GetDrawableSize(gSDLImageWindow, &w, &h);
                    reshapeImageWindow(w, h);
                }
            } else if (ev.type == SDL_KEYDOWN) {
                keyboard(ev.key.keysym.sym);
            }
        }
        
    } // !done
    
    quit(0);
    return 0;
}

static void reshapeImageWindow(int w, int h)
{
    gImageContextWidth = w;
    gImageContextHeight = h;
    ARLOGd("Resized to %dx%d.\n", w, h);
    drawImageView();
}

static void quit(int rc)
{
    arglCleanup(gArglContextSettings);
    refImage = nullptr;

    free(inputFilePath);

    SDL_Quit();
    
    exit(rc);
}

static void loadImage(void)
{
    ARPRINT("Loading image data %s.\n", inputFilePath);
    try {
        int nc;
        if (!ReadImageFromFile(inputFilePath, refImage, &refImageX, &refImageY, &nc, true)) {
            ARLOGe("Unable to load image '%s'.\n", inputFilePath);
            exit(0);
        }
    } catch (std::runtime_error) { // File not found.
        ARLOGe("file open error: %s.iset\n", inputFilePath);
        exit(0);
    }
    ARPRINT("  end.\n");
}

static void getImageFeature()
{
    if (display_templates || display_features) {
        cv::Mat image = cv::Mat(refImageY, refImageX, CV_8UC1, refImage.get());
        if (display_features) {
            ARPRINT("Generating features...\n");
            OCVFeatureDetector _featureDetector = OCVFeatureDetector();
            _featureDetector.SetFeatureDetector(defaultDetectorType);
            _featurePoints = _featureDetector.DetectFeatures(image, cv::Mat());
            ARPRINT("Number of features = %zu.\n", _featurePoints.size());
        }
        if (display_templates) {
            ARPRINT("Generating templates...\n");
            HarrisDetector _harrisDetector = HarrisDetector();
            for (int i = 0; i <= k_OCVTTemplateMatchingMaxPyrLevel; i++) {
                if (i > 0) cv::pyrDown(image, image);
                std::vector<cv::Point2f> _cornerPoints = _harrisDetector.FindCorners(image);
                _trackSelection[i] = TrackingPointSelector(_cornerPoints, image.cols, image.rows, markerTemplateWidth, refImageX, refImageY);
                _templatePoints[i] = _trackSelection[i].GetAllFeatures();
                ARPRINT("Number of templates (level %d, image size %dx%d) = %zu.\n", i, image.cols, image.rows, _templatePoints[i].size());
            }
        }
    }
    setTemplatePyrLevel(0);
}

static void setTemplatePyrLevel(int templatePyrLevel)
{
    _templatePyrLevel = templatePyrLevel % (k_OCVTTemplateMatchingMaxPyrLevel + 1);

    // Update the image to be drawn.
    if (gArglContextSettings) {
        arglCleanup(gArglContextSettings);
        gArglContextSettings = NULL;
    }
    ARParam cparam;
    arParamClear(&cparam, refImageX, refImageY, AR_DIST_FUNCTION_VERSION_DEFAULT);
    gArglContextSettings = arglSetupForCurrentContext(&cparam, AR_PIXEL_FORMAT_MONO);
    arglDistortionCompensationSet(gArglContextSettings, FALSE);
    arglPixelBufferDataUpload(gArglContextSettings, refImage.get());
    drawImageView();
}

static void processCommandLineOptions(int argc, char *argv[])
{
    int i, gotTwoPartOption, display_defaults = 1;
    //int tempI;
    
    //
    // Command-line options.
    //
    
    i = 1; // argv[0] is name of app, so start at 1.
    while (i < argc) {
        gotTwoPartOption = FALSE;
        // Look for two-part options first.
        if ((i + 1) < argc) {
            //if (strcmp(argv[i], "--someOption") == 0) {
            //    i++;
            //    if (sscanf(argv[i], "%d", &tempI) == 1 && tempI > 0) gSomeVar = tempI;
            //    else ARPRINTE("Error: argument '%s' to --someOption invalid.\n", argv[i]);
            //    gotTwoPartOption = TRUE;
            //}
        }
        if (!gotTwoPartOption) {
            // Look for single-part options.
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "-h") == 0) {
                usage(argv[0]);
            } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-version") == 0 || strcmp(argv[i], "-v") == 0) {
                ARPRINT("%s version %s\n", argv[0], AR_HEADER_VERSION_STRING);
                exit(0);
            } else if( strncmp(argv[i], "-loglevel=", 10) == 0 ) {
                if (strcmp(&(argv[i][10]), "DEBUG") == 0) arLogLevel = AR_LOG_LEVEL_DEBUG;
                else if (strcmp(&(argv[i][10]), "INFO") == 0) arLogLevel = AR_LOG_LEVEL_INFO;
                else if (strcmp(&(argv[i][10]), "WARN") == 0) arLogLevel = AR_LOG_LEVEL_WARN;
                else if (strcmp(&(argv[i][10]), "ERROR") == 0) arLogLevel = AR_LOG_LEVEL_ERROR;
                else usage(argv[0]);
            } else if (strcmp(argv[i], "-templates") == 0) {
                display_templates = 1;
            } else if (strcmp(argv[i], "-notemplates") == 0) {
                display_templates = 0;
            } else if (strcmp(argv[i], "-features") == 0) {
                display_features = 1;
            } else if (strcmp(argv[i], "-nofeatures") == 0) {
                display_features = 0;
            } else if( strcmp(argv[i], "-bins") == 0 ) {
                display_bins = 1;
            } else if( strcmp(argv[i], "-nobins") == 0 ) {
                display_bins = 0;
            } else {
                if (!inputFilePath) inputFilePath = strdup(argv[i]);
                else usage(argv[0]);
            }
        }
        i++;
    }
    if (!inputFilePath) usage(argv[0]);
}

static void usage( char *com )
{
    ARPRINT("Usage: %s [options] <filename>\n\n", com);
    ARPRINT("Where <filename> is path to a JPEG or iset file.\n\n");
    ARPRINT("Options:\n");
    ARPRINT("  -[no]features   Show [or don't show] tracking features.\n");
    ARPRINT("  -[no]templates  Show [or don't show] tracking templates.\n");
    ARPRINT("  -[no]bins       Show [or don't show] tracking bins.\n");
    ARPRINT("  --version: Print artoolkitX version and exit.\n");
    ARPRINT("  -loglevel=l: Set the log level to l, where l is one of DEBUG INFO WARN ERROR.\n");
    ARPRINT("  -h -help --help: show this message\n");
    exit(0);
}

static void keyboard(SDL_Keycode key)
{
    bool redraw = false;
    
    switch (key) {
        case 0x1B:						// Quit.
        case 'Q':
        case 'q':
            quit(0);
            break;
        case ' ':
            setTemplatePyrLevel(_templatePyrLevel + 1);
            break;
        case '?':
        case '/':
            gShowHelp++;
            if (gShowHelp > 1) gShowHelp = 0;
            redraw = true;
            break;
        case 'm':
        case 'M':
            gShowMode = !gShowMode;
            redraw = true;
            break;
        default:
            break;
    }
    if (redraw) {
        drawImageView();
    }
}

static float calcZoomToFit(int sourceSizeX, int sourceSizeY, int destSizeX, int destSizeY)
{
    float xzoom, yzoom;
    xzoom = (float)destSizeX / (float)sourceSizeX;
    yzoom = (float)destSizeY / (float)sourceSizeY;
    return (xzoom > yzoom ? yzoom : xzoom);
}

//
// This function is called when the window needs redrawing.
//
static void drawImageView(void)
{
    int i;
    int viewport[4];
    
    if (!refImage || !gArglContextSettings) return;
    
    SDL_GL_MakeCurrent(gSDLImageWindow, gSDLImageContext);
    
    imageZoom = calcZoomToFit(refImageX, refImageY, gImageContextWidth, gImageContextHeight);
    ARPRINT("%dx%d input image will display in %dx%d window at %.1f%% size\n", refImageX, refImageY, gImageContextWidth, gImageContextHeight, imageZoom*100.0f);
    
    viewport[0] = 0;
    viewport[1] = 0;
    viewport[2] = (int)(refImageX * imageZoom);
    viewport[3] = (int)(refImageY * imageZoom);
    
    // Clean the OpenGL context.
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    arglDispImage(gArglContextSettings, viewport);
    
    // 2D overlays in image frame.
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, (GLdouble)refImageX, 0, (GLdouble)refImageY, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    
    EdenGLFontSetViewSize(refImageX, refImageY);
    
    if (display_templates) {

        // Draw red boxes around template features, and number.
        glLineWidth(2.0f);
        float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        glColor4fv(red);
        EdenGLFontSetColor(red);

        float templateRadius = (markerTemplateWidth << _templatePyrLevel)/2.0f;
        
        for (i = 0; i < _templatePoints[_templatePyrLevel].size(); i++) {
            int x = _templatePoints[_templatePyrLevel][i].x;
            int y = refImageY - _templatePoints[_templatePyrLevel][i].y; // OpenGL y-origin is at bottom, tracker y origin is at top.
            
            GLfloat vertices[4][2];
            vertices[0][0] = x - templateRadius;
            vertices[0][1] = y - templateRadius;
            vertices[1][0] = x - templateRadius;
            vertices[1][1] = y + templateRadius;
            vertices[2][0] = x + templateRadius;
            vertices[2][1] = y + templateRadius;
            vertices[3][0] = x + templateRadius;
            vertices[3][1] = y - templateRadius;
            glVertexPointer(2, GL_FLOAT, 0, vertices);
            glEnableClientState(GL_VERTEX_ARRAY);
            glDrawArrays(GL_LINE_LOOP, 0, 4);

            // Individually number templates onscreen.
            //glLineWidth(1.0f);
            //char text[16];
            //snprintf(text, sizeof(text), "%d", i);
            //EdenGLFontDrawLine(0, NULL, (unsigned char *)text, (float)x, (float)y, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_VIEW_BOTTOM_TO_TEXT_BASELINE);
            
            glDisableClientState(GL_VERTEX_ARRAY);
        }
    }
    
    if (display_bins) {
 
        // Draw Lines for bins.
        const int numBins = 10;
        GLfloat vertices[(numBins + 1)*4][2];
        for (int i = 0; i <= numBins; i++) {
            vertices[i * 4    ][0] = refImageX * i / (float)numBins;
            vertices[i * 4    ][1] = 0;
            vertices[i * 4 + 1][0] = refImageX * i / (float)numBins;
            vertices[i * 4 + 1][1] = refImageY;
            vertices[i * 4 + 2][0] = 0;
            vertices[i * 4 + 2][1] = refImageY * i / (float)numBins;
            vertices[i * 4 + 3][0] = refImageX;
            vertices[i * 4 + 3][1] = refImageY * i / (float)numBins;
        }        
        glLineWidth(1.0f);
        float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        glColor4fv(blue);
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glEnableClientState(GL_VERTEX_ARRAY);
        glDrawArrays(GL_LINES, 0, (numBins + 1)*4);
    }
    
    if (display_features) {
        
        // Draw green crosses on features.
        glLineWidth(2.0f);
        float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        glColor4fv(green);
        
        for (int i = 0; i < _featurePoints.size(); i++) {
            int x = _featurePoints[i].pt.x;
            int y = refImageY - _featurePoints[i].pt.y; // OpenGL y-origin is at bottom, tracker y origin is at top.
            GLfloat vertices[4][2];
            vertices[0][0] = x - 5;
            vertices[0][1] = y - 5;
            vertices[1][0] = x + 5;
            vertices[1][1] = y + 5;
            vertices[2][0] = x + 5;
            vertices[2][1] = y - 5;
            vertices[3][0] = x - 5;
            vertices[3][1] = y + 5;
            glVertexPointer(2, GL_FLOAT, 0, vertices);
            glEnableClientState(GL_VERTEX_ARRAY);
            glDrawArrays(GL_LINES, 0, 4);
            glDisableClientState(GL_VERTEX_ARRAY);
        }
    }
    
    // 2D overlays in context space.
    glViewport(0, 0, gImageContextWidth, gImageContextHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, (GLdouble)gImageContextWidth, 0, (GLdouble)gImageContextHeight, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    EdenGLFontSetViewSize(gImageContextWidth, gImageContextHeight);
    glLineWidth(1.0f);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    EdenGLFontSetColor(white);
    EdenGLFontSetSize(FONT_SIZE);

    // Draw help text and mode.
    if (gShowMode) {
        printMode();
    }
    if (gShowHelp) {
        if (gShowHelp == 1) {
            printHelpKeys();
        }
    }
    
    SDL_GL_SwapWindow(gSDLImageWindow);
}

//
// The following functions provide the onscreen help text and mode info.
//

static void drawBackground(const float width, const float height, const float x, const float y)
{
    GLfloat vertices[4][2];
    
    vertices[0][0] = x; vertices[0][1] = y;
    vertices[1][0] = width + x; vertices[1][1] = y;
    vertices[2][0] = width + x; vertices[2][1] = height + y;
    vertices[3][0] = x; vertices[3][1] = height + y;
    glLoadIdentity();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);	// 50% transparent black.
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Opaque white.
    //glLineWidth(1.0f);
    //glDrawArrays(GL_LINE_LOOP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
}

static void printHelpKeys()
{
    GLfloat bw, bh;
    const char *helpText[] = {
        "Keys:\n",
        " ? or /        Show/hide this help.",
        " q or [esc]    Quit program.",
        " [space]       Page through all template resolutions."
    };
#define helpTextLineCount (sizeof(helpText)/sizeof(char *))
    
    bw = EdenGLFontGetBlockWidth((const unsigned char **)helpText, helpTextLineCount);
    bh = EdenGLFontGetBlockHeight(helpTextLineCount);
    drawBackground(bw, bh, 2.0f, 2.0f);
    glDisable(GL_BLEND);
    EdenGLFontDrawBlock(0, NULL, (const unsigned char **)helpText, helpTextLineCount, 2.0f, 2.0f, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_VIEW_BOTTOM_TO_TEXT_BASELINE);
}

static void printMode()
{
    int line;
    char text[256];
    
    glColor3ub(255, 255, 255);
    line = 1;
    
    // Feature set resolution.
    snprintf(text, sizeof(text), /*"%f[dpi] "*/"image. Size = (%d,%d)\n", /* dpi ,*/
             refImageX,
             refImageY);
    EdenGLFontDrawLine(0, NULL, (unsigned char *)text, 2.0f,  (line - 1)*FONT_SIZE + 2.0f, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_TEXT_TOP_TO_VIEW_TOP);
    line++;

    if (display_features) {
        snprintf(text, sizeof(text), "Num of feature points: %ld\n", _featurePoints.size());
        EdenGLFontDrawLine(0, NULL, (unsigned char *)text, 2.0f,  (line - 1)*FONT_SIZE + 2.0f, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_TEXT_TOP_TO_VIEW_TOP);
        line++;
    }

    if (display_templates) {
        snprintf(text, sizeof(text), "Number of templates (at level %d): %ld\n", _templatePyrLevel, _templatePoints[_templatePyrLevel].size());
        EdenGLFontDrawLine(0, NULL, (unsigned char *)text, 2.0f,  (line - 1)*FONT_SIZE + 2.0f, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_TEXT_TOP_TO_VIEW_TOP);
        line++;
    }
    
    // Window size.
    snprintf(text, sizeof(text), "Drawing into %dx%d window", gImageContextWidth, gImageContextHeight);
    EdenGLFontDrawLine(0, NULL, (unsigned char *)text, 2.0f,  (line - 1)*FONT_SIZE + 2.0f, H_OFFSET_VIEW_LEFT_EDGE_TO_TEXT_LEFT_EDGE, V_OFFSET_TEXT_TOP_TO_VIEW_TOP);
    line++;

}
