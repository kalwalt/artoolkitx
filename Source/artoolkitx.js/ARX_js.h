// #include <emscripten/bind.h>
#include "ARX/ARX_c.h"
#include <string>
#include "ARX/ARController.h"

// #include "ARX_c.cpp"

std::string getARToolKitVersion();
int addTrackable(std::string cfg);
bool arwStartRunningJS(std::string cparaName, int width, int height);
int pushVideoInit(int videoSourceIndex, int width, int height, std::string pixelFormat, int camera_index, int camera_face);

struct VideoParams {
    int width;
    int height;
    int pixelSize;
    std::string pixelFormat;
};
VideoParams getVideoParams();

static ARMarkerInfo gMarkerInfo;
static int MARKER_INDEX_OUT_OF_BOUNDS = -3;

int getMarkerInfo(int id, int markerIndex);
