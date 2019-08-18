/**
 *
 *
 */

#include "ARX_js.h"
#include <stdio.h>
#include "emscripten.h"
#include <ARX/AR/ar.h>

#define PIXEL_FORMAT_BUFFER_SIZE 1024

std::string getARToolKitVersion(){
    char versionString[1024];
    std::string returnValue ("unknown version");
    if (arwGetARToolKitVersion(versionString, 1024)){
        return std::string(versionString);
    }
    return returnValue;
}

int addTrackable(std::string cfg) {
    return arwAddTrackable(cfg.c_str());
}

    /**
     * Initialises and starts video capture.
     *
     * @param cparamName    The URL to the camera parameter file. NULL if none required or if using an image as input
     * @param width         The width of the video frame/image to process
     * @param height        The height of the video frame/image to process
     * @return              true if successful, false if an error occurred
     * @see                 arwStopRunning()
     */
    bool arwStartRunningJS(std::string cparaName, int width, int height) {
        char buffer [50];
        sprintf(buffer,"-module=Web -width=%d -height=%d", width, height);
        int ret;

        if( cparaName.empty()) {
            ret = arwStartRunning(buffer, NULL);
        }
        else {
            ret = arwStartRunning(buffer, cparaName.c_str());
        }

        return ret;
    }

    int pushVideoInit(int videoSourceIndex, int width, int height, std::string pixelFormat, int camera_index, int camera_face){
        return arwVideoPushInitWeb(videoSourceIndex, width, height, pixelFormat.c_str(), camera_index, camera_face);
    }


VideoParams getVideoParams() {
    int w, h, ps;
    char pf[PIXEL_FORMAT_BUFFER_SIZE];
    VideoParams videoParams;
    if( !arwGetVideoParams(&w, &h, &ps, pf, PIXEL_FORMAT_BUFFER_SIZE))
        return videoParams;
    else {
        videoParams.width = w;
        videoParams.height = h;
        videoParams.pixelSize = ps;
        videoParams.pixelFormat = std::string(pf);
    }
    return videoParams;
}

//TODO: to be implemented
// bool getTrackables() {
//     int count;
//     ARWTrackableStatus status;
//     arwGetTrackables(&count, status)
// }

int getMarkerInfo(int id, int markerIndex) {
  ARHandle *arhandle;
  if (arhandle->marker_num <= markerIndex) {
    return MARKER_INDEX_OUT_OF_BOUNDS;
  }
  ARMarkerInfo* markerInfo = markerIndex < 0 ? &gMarkerInfo : &((arhandle)->markerInfo[markerIndex]);

  EM_ASM_({
    var $a = arguments;
    var i = 12;
    if (!artoolkitXjs["markerInfo"]) {
      artoolkitXjs["markerInfo"] = ({
        pos: [0,0],
        line: [[0,0,0], [0,0,0], [0,0,0], [0,0,0]],
        vertex: [[0,0], [0,0], [0,0], [0,0]]
      });
    }
    var markerInfo = artoolkitXjs["markerInfo"];
    markerInfo["area"] = $0;
    markerInfo["id"] = $1;
    markerInfo["idPatt"] = $2;
    markerInfo["idMatrix"] = $3;
    markerInfo["dir"] = $4;
    markerInfo["dirPatt"] = $5;
    markerInfo["dirMatrix"] = $6;
    markerInfo["cf"] = $7;
    markerInfo["cfPatt"] = $8;
    markerInfo["cfMatrix"] = $9;
    markerInfo["pos"][0] = $10;
    markerInfo["pos"][1] = $11;
    markerInfo["line"][0][0] = $a[i++];
    markerInfo["line"][0][1] = $a[i++];
    markerInfo["line"][0][2] = $a[i++];
    markerInfo["line"][1][0] = $a[i++];
    markerInfo["line"][1][1] = $a[i++];
    markerInfo["line"][1][2] = $a[i++];
    markerInfo["line"][2][0] = $a[i++];
    markerInfo["line"][2][1] = $a[i++];
    markerInfo["line"][2][2] = $a[i++];
    markerInfo["line"][3][0] = $a[i++];
    markerInfo["line"][3][1] = $a[i++];
    markerInfo["line"][3][2] = $a[i++];
    markerInfo["vertex"][0][0] = $a[i++];
    markerInfo["vertex"][0][1] = $a[i++];
    markerInfo["vertex"][1][0] = $a[i++];
    markerInfo["vertex"][1][1] = $a[i++];
    markerInfo["vertex"][2][0] = $a[i++];
    markerInfo["vertex"][2][1] = $a[i++];
    markerInfo["vertex"][3][0] = $a[i++];
    markerInfo["vertex"][3][1] = $a[i++];
    markerInfo["errorCorrected"] = $a[i++];
    // markerInfo["globalID"] = $a[i++];
  },
    markerInfo->area,
    markerInfo->id,
    markerInfo->idPatt,
    markerInfo->idMatrix,
    markerInfo->dir,
    markerInfo->dirPatt,
    markerInfo->dirMatrix,
    markerInfo->cf,
    markerInfo->cfPatt,
    markerInfo->cfMatrix,

    markerInfo->pos[0],
    markerInfo->pos[1],

    markerInfo->line[0][0],
    markerInfo->line[0][1],
    markerInfo->line[0][2],

    markerInfo->line[1][0],
    markerInfo->line[1][1],
    markerInfo->line[1][2],

    markerInfo->line[2][0],
    markerInfo->line[2][1],
    markerInfo->line[2][2],

    markerInfo->line[3][0],
    markerInfo->line[3][1],
    markerInfo->line[3][2],

    //

    markerInfo->vertex[0][0],
    markerInfo->vertex[0][1],

    markerInfo->vertex[1][0],
    markerInfo->vertex[1][1],

    markerInfo->vertex[2][0],
    markerInfo->vertex[2][1],

    markerInfo->vertex[3][0],
    markerInfo->vertex[3][1],

    //

    markerInfo->errorCorrected

    // markerInfo->globalID
  );

  return 0;
}

int getTransMatSquareCont(int id, int markerIndex, int markerWidth) {

  ARHandle *arhandle;
  AR3DHandle *ar3DHandle;

  if (arhandle->marker_num <= markerIndex) {
    return MARKER_INDEX_OUT_OF_BOUNDS;
  }
  ARMarkerInfo* marker = markerIndex < 0 ? &gMarkerInfo : &((arhandle)->markerInfo[markerIndex]);

  arGetTransMatSquareCont(ar3DHandle, marker, gTransform, markerWidth, gTransform);

  return 0;
}
