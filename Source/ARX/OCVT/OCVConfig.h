/*
 *  OCVConfig.h
 *  artoolkitX
 *
 *  This file is part of artoolkitX.
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
 *  Copyright 2010-2015 ARToolworks, Inc.
 *
 *  Author(s): Philip Lamb, Daniel Bell.
 *
 */

#ifndef OCV_CONFIG_H
#define OCV_CONFIG_H

#ifdef _WIN32
#  ifdef OCV_STATIC
#    define OCV_EXTERN
#  else
#    ifdef ARX_EXPORTS
#      define OCV_EXTERN __declspec(dllexport)
#    else
#      define OCV_EXTERN __declspec(dllimport)
#    endif
#  endif
#  define OCV_CALLBACK __stdcall
#else
#  define OCV_EXTERN
#  define OCV_CALLBACK
#endif

#include <opencv2/core.hpp>
//#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <ARX/OCVT/PlanarTracker.h>

/** @file */

/// @def k_OCVTOpticalFlowMaxPyrLevel Maximum number of levels in optical flow image pyramid (0 = base level only).
#define k_OCVTOpticalFlowMaxPyrLevel 3
/// @def k_OCVTTemplateMatchingMaxPyrLevel Maximum number of levels in template matching image pyramid (0 = base level only).
#define k_OCVTTemplateMatchingMaxPyrLevel 2

OCV_EXTERN extern int minRequiredDetectedFeatures; ///< Minimum number of detected features required to consider a target matched.
OCV_EXTERN extern const int markerTemplateWidth; ///< Width in pixels of image patches used in template matching.
OCV_EXTERN extern const cv::Size subPixWinSize;
OCV_EXTERN extern const cv::Size winSize; ///< Window size to use in optical flow search.
OCV_EXTERN extern cv::TermCriteria termcrit;
OCV_EXTERN extern const int markerTemplateCountMax; ///< Maximum number of Harris corners to use as template locations.  If <= 0, no limit on the maximum is set and all detected corners will be used.
OCV_EXTERN extern const int searchRadius;
OCV_EXTERN extern const int match_method;
OCV_EXTERN extern const cv::Size featureImageMinSize; ///< Minimum size when downscaling incoming images used for feature tracking.
OCV_EXTERN extern PlanarTracker::FeatureDetectorType defaultDetectorType;
OCV_EXTERN extern const double nn_match_ratio; ///< Nearest-neighbour matching ratio
OCV_EXTERN extern double ransac_thresh; ///< RANSAC inlier threshold
OCV_EXTERN extern cv::RNG rng;
OCV_EXTERN extern const int harrisBorder; ///< Harris corners within this many pixels of the border of the image will be ignored.

#endif // OCV_CONFIG_H
