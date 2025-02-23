/*
 *  PlanarTracker.cpp
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

#include "PlanarTracker.h"

#include "OCVConfig.h"
#include "OCVFeatureDetector.h"
#include "HarrisDetector.h"
#include "TrackableInfo.h"
#include "HomographyInfo.h"
#include "OCVUtils.h"
#include "TrackerVisualization.h"
#include <opencv2/video.hpp>
#include <iostream>
#include <algorithm>

class PlanarTracker::PlanarTrackerImpl
{
private:
    int _maxNumberOfMarkersToTrack;
    OCVFeatureDetector _featureDetector;
    HarrisDetector _harrisDetector;
    std::vector<cv::Mat> _pyramid, _prevPyramid;
    
    std::vector<TrackableInfo> _trackables;
    
    int _currentlyTrackedMarkers;
    int _resetCount;
    int _frameCount;
    int _frameSizeX;
    int _frameSizeY;
    /// Pyramid level used in downsampling incoming image for feature matching. 0 = no size change, 1 = half width/height, 2 = quarter width/heigh etc.
    int _featureDetectPyrLevel;
    /// Scale factor applied to images used for feature matching. Will be 2^_featureDetectPyrLevel.
    cv::Vec2f _featureDetectScaleFactor;
    cv::Mat _K;
    cv::Mat _distortionCoeff;

    FeatureDetectorType _selectedFeatureDetectorType;
        
public:
    bool _trackVizActive;
    TrackerVisualization _trackViz;

    PlanarTrackerImpl() :
        _maxNumberOfMarkersToTrack(1),
        _featureDetector(OCVFeatureDetector()),
        _harrisDetector(HarrisDetector()),
        _currentlyTrackedMarkers(0),
        _frameCount(0),
        _resetCount(30),
        _frameSizeX(0),
        _frameSizeY(0),
        _featureDetectPyrLevel(0),
        _featureDetectScaleFactor(cv::Vec2f(1.0f, 1.0f)),
        _K(cv::Mat()),
        _distortionCoeff(cv::Mat()),
        _trackVizActive(false),
        _trackViz(TrackerVisualization())
    {
        SetFeatureDetector(defaultDetectorType);
    }
    
    void Initialise(ARParam cParam)
    {
        _frameSizeX = cParam.xsize;
        _frameSizeY = cParam.ysize;
        
        // Calculate image downsamping factor. 0 = no size change, 1 = half width and height, 2 = quarter width and height etc.
        double xmin_log2 = std::log2(static_cast<double>(featureImageMinSize.width));
        double ymin_log2 = std::log2(static_cast<double>(featureImageMinSize.height));
        _featureDetectPyrLevel = std::min(std::floor(std::log2(static_cast<double>(_frameSizeX)) - xmin_log2), std::floor(std::log2(static_cast<double>(_frameSizeY)) - ymin_log2));
        _featureDetectScaleFactor = CalcPyrDownScaleFactor(_featureDetectPyrLevel, _frameSizeX, _frameSizeY);

        _K = cv::Mat(3,3, CV_64FC1);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                _K.at<double>(i,j) = (double)(cParam.mat[i][j]);
            }
        }

        if (cParam.dist_function_version == 5) {
            // k1,k2,p1,p2,k3,k4,k5,k6,s1,s2,s3,s4.
            _distortionCoeff = cv::Mat::zeros(12, 1, CV_64F);
            for (int i = 0; i < 12; i++) _distortionCoeff.at<double>(i) = cParam.dist_factor[i];
        } else if (cParam.dist_function_version == 4) {
            _distortionCoeff = cv::Mat::zeros(5, 1, CV_64F);
            // k1,k2,p1,p2, and k3=0.
            for (int i = 0; i < 4; i++) _distortionCoeff.at<double>(i) = cParam.dist_factor[i];
            _distortionCoeff.at<double>(4) = 0.0;
        } else {
            ARLOGw("Unsupported camera parameters.\n");
        }
        
        _pyramid.clear();
        _prevPyramid.clear();
        _currentlyTrackedMarkers = 0;
    }
    
    /// Calculate the exact scale factor using the same calculation pyrDown uses.
    cv::Vec2f CalcPyrDownScaleFactor(int pyrLevel, int x, int y)
    {
        cv::Vec2f ret = cv::Vec2f(1.0f, 1.0f);
        int xScaled = x;
        int yScaled = y;
        for (int i = 1; i <= pyrLevel; i++) {
            xScaled = (xScaled + 1) / 2;
            yScaled = (yScaled + 1) / 2;
            ret = cv::Vec2f((float)x / (float)xScaled, (float)y / (float)yScaled);
        }
        return ret;
    }

    /// Creates a mask image where the areas occupied by all currently tracked markers are 0, and all areas
    /// outside the markers are 1.
    cv::Mat CreateFeatureMask(cv::Mat frame)
    {
        cv::Mat featureMask;
        for (int i = 0; i < _trackables.size(); i++) {
            if (_trackables[i]._isDetected) {
                if (featureMask.empty()) {
                    //Only create mask if we have something to draw in it.
                    featureMask = cv::Mat::ones(frame.size(), CV_8UC1);
                }
                std::vector<std::vector<cv::Point> > contours(1);
                for (int j = 0; j < 4; j++) {
                    contours[0].push_back(cv::Point(_trackables[i]._bBoxTransformed[j].x/_featureDetectScaleFactor[0],_trackables[i]._bBoxTransformed[j].y/_featureDetectScaleFactor[1]));
                }
                drawContours(featureMask, contours, 0, cv::Scalar(0), cv::LineTypes::FILLED, cv::LineTypes::LINE_8);
            }
        }
        return featureMask;
    }
    
    void UpdateTrackableBBox(const int index, const cv::Mat& homography)
    {
        perspectiveTransform(_trackables[index]._bBox, _trackables[index]._bBoxTransformed, homography);
        if (_trackVizActive) {
            for (int i = 0; i < 4; i++) {
                _trackViz.bounds[i][0] = _trackables[index]._bBoxTransformed[i].x;
                _trackViz.bounds[i][1] = _trackables[index]._bBoxTransformed[i].y;
            }
        }
    }
    
    void MatchFeatures(const std::vector<cv::KeyPoint>& newFrameFeatures, cv::Mat newFrameDescriptors)
    {
        int maxMatches = 0;
        int bestMatchIndex = -1;
        std::vector<cv::KeyPoint> finalMatched1, finalMatched2;
        for (int i = 0; i < _trackables.size(); i++) {
            if (!_trackables[i]._isDetected) {
                std::vector< std::vector<cv::DMatch> >  matches = _featureDetector.MatchFeatures(newFrameDescriptors, _trackables[i]._descriptors);
                if (matches.size() > minRequiredDetectedFeatures) {
                    std::vector<cv::KeyPoint> matched1, matched2;
                    std::vector<uchar> status;
                    int totalGoodMatches = 0;
                    for (unsigned int j = 0; j < matches.size(); j++) {
                        // Ratio Test for outlier removal, removes ambiguous matches.
                        if (matches[j][0].distance < nn_match_ratio * matches[j][1].distance) {
                            matched1.push_back(newFrameFeatures[matches[j][0].queryIdx]);
                            matched2.push_back(_trackables[i]._featurePoints[matches[j][0].trainIdx]);
                            status.push_back(1);
                            totalGoodMatches++;
                        } else {
                            status.push_back(0);
                        }
                    }
                    // Measure goodness of match by most number of matching features.
                    // This allows for maximum of a single marker to match each time.
                    // TODO: Would a better metric be percentage of marker features matching?
                    if (totalGoodMatches > maxMatches) {
                        finalMatched1 = matched1;
                        finalMatched2 = matched2;
                        maxMatches = totalGoodMatches;
                        bestMatchIndex = i;
                    }
                }
            }
        }
        
        if (maxMatches > 0) {
            for (int i = 0; i < finalMatched1.size(); i++) {
                finalMatched1[i].pt.x *= _featureDetectScaleFactor[0];
                finalMatched1[i].pt.y *= _featureDetectScaleFactor[1];
            }
            
            HomographyInfo homoInfo = GetHomographyInliers(Points(finalMatched2), Points(finalMatched1));
            if (homoInfo.validHomography) {
                //std::cout << "New marker detected" << std::endl;
                _trackables[bestMatchIndex]._isDetected = true;
                _trackables[bestMatchIndex]._resetTracks = true;
                // Since we've just detected the marker, make sure next invocation of
                // GetInitialFeatures() for this marker makes a new selection.
                ResetAllTrackingPointSelectorsForTrackable(bestMatchIndex);
                _trackables[bestMatchIndex]._homography = homoInfo.homography;
                
                UpdateTrackableBBox(bestMatchIndex, homoInfo.homography); // Initial estimate of the bounding box, which will be refined by the optical flow pass.

                _currentlyTrackedMarkers++;
            }
        }
    }
    
    void ResetAllTrackingPointSelectorsForTrackable(int trackableIndex)
    {
        for (int i = 0; i <= k_OCVTTemplateMatchingMaxPyrLevel; i++) {
            _trackables[trackableIndex]._trackSelection[i].ResetSelection();
        }
    }
        
    bool RunOpticalFlow(int trackableId, const std::vector<cv::Point2f>& trackablePoints, const std::vector<cv::Point2f>& trackablePointsWarped)
    {
        std::vector<cv::Point2f> flowResultPoints, trackablePointsWarpedResult;
        std::vector<uchar> statusFirstPass, statusSecondPass;
        std::vector<float> err;
        cv::calcOpticalFlowPyrLK(_prevPyramid, _pyramid, trackablePointsWarped, flowResultPoints, statusFirstPass, err, winSize, k_OCVTOpticalFlowMaxPyrLevel, termcrit, 0, 0.001);
        // By using bi-directional optical flow, we improve quality of detected points.
        cv::calcOpticalFlowPyrLK(_pyramid, _prevPyramid, flowResultPoints, trackablePointsWarpedResult, statusSecondPass, err, winSize, k_OCVTOpticalFlowMaxPyrLevel, termcrit, 0, 0.001);
        
        // Keep only the points for which flow was found in both temporal directions.
        int killed1 = 0;
        std::vector<cv::Point2f> filteredTrackablePoints, filteredTrackedPoints;
        for (auto j = 0; j != flowResultPoints.size(); ++j) {
            if (!statusFirstPass[j] || !statusSecondPass[j]) {
                statusFirstPass[j] = (uchar)0;
                killed1++;
                continue;
            }
            filteredTrackablePoints.push_back(trackablePoints[j]);
            filteredTrackedPoints.push_back(flowResultPoints[j]);
        }
        if (_trackVizActive) {
            _trackViz.opticalFlowTrackablePoints = filteredTrackablePoints;
            _trackViz.opticalFlowTrackedPoints = filteredTrackedPoints;
        }
        //std::cout << "Optical flow discarded " << killed1 << " of " << flowResultPoints.size() << " points" << std::endl;

        if (!UpdateTrackableHomography(trackableId, filteredTrackablePoints, filteredTrackedPoints)) {
            _trackables[trackableId]._isDetected = false;
            _trackables[trackableId]._isTracking = false;
            _currentlyTrackedMarkers--;
            return false;
        }

        _trackables[trackableId]._isTracking = true;
        return true;
    }
    
    bool UpdateTrackableHomography(int trackableId, const std::vector<cv::Point2f>& matchedPoints1, const std::vector<cv::Point2f>& matchedPoints2)
    {
        if (matchedPoints1.size() > 4) {
            HomographyInfo homoInfo = GetHomographyInliers(matchedPoints1, matchedPoints2);
            if (homoInfo.validHomography) {
                _trackables[trackableId]._trackSelection[_trackables[trackableId]._templatePyrLevel].UpdatePointStatus(homoInfo.status);
                _trackables[trackableId]._homography = homoInfo.homography;
                UpdateTrackableBBox(trackableId, homoInfo.homography);
                if (_frameCount > 1) {
                    ResetAllTrackingPointSelectorsForTrackable(trackableId);
                }
                return true;
            }
        }
        return false;
    }
    
    /// @brief Calculates vertices of a rect centered on ptOrig.
    std::vector<cv::Point2f> GetVerticesFromPoint(cv::Point ptOrig, int width, int height)
    {
        std::vector<cv::Point2f> vertexPoints;
        vertexPoints.push_back(cv::Point2f(ptOrig.x - width/2, ptOrig.y - height/2));
        vertexPoints.push_back(cv::Point2f(ptOrig.x + width/2, ptOrig.y - height/2));
        vertexPoints.push_back(cv::Point2f(ptOrig.x + width/2, ptOrig.y + height/2));
        vertexPoints.push_back(cv::Point2f(ptOrig.x - width/2, ptOrig.y + height/2));
        return vertexPoints;
    }
    
    /// @brief Calculates vertices of a rect with its top corner located at (x, y).
    std::vector<cv::Point2f> GetVerticesFromTopCorner(int x, int y, int width, int height)
    {
        std::vector<cv::Point2f> vertexPoints;
        vertexPoints.push_back(cv::Point2f(x, y));
        vertexPoints.push_back(cv::Point2f(x + width, y));
        vertexPoints.push_back(cv::Point2f(x + width, y + height));
        vertexPoints.push_back(cv::Point2f(x, y + height));
        return vertexPoints;
    }
    
    cv::Rect GetTemplateRoi(cv::Point2f pt)
    {
        return cv::Rect(pt.x - (markerTemplateWidth/2), pt.y - (markerTemplateWidth/2), markerTemplateWidth, markerTemplateWidth);
    }
    
    bool IsRoiValidForFrame(cv::Rect frameRoi, cv::Rect roi)
    {
        return (roi & frameRoi) == roi;
    }
    
    /// @brief Inflate the region of interest bounds by inflationFactor on each side.
    /// @return The inflated bounds.
    cv::Rect InflateRoi(const cv::Rect& roi, int inflationFactor)
    {
        cv::Rect newRoi = roi;
        newRoi.x -= inflationFactor;
        newRoi.y -= inflationFactor;
        newRoi.width += 2 * inflationFactor;
        newRoi.height += 2 * inflationFactor;
        return newRoi;
    }
    
    /// @brief Transform all vertices by the same amount such that the point with the lowest x value moves to x=0
    /// and the point with the lowest y value moves to y=0.
    /// @return The transformed points.
    std::vector<cv::Point2f> FloorVertexPoints(const std::vector<cv::Point2f>& vertexPoints)
    {
        std::vector<cv::Point2f> testVertexPoints = vertexPoints;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        for (int k = 0; k < testVertexPoints.size(); k++) {
            if (testVertexPoints[k].x < minX) {
                minX = testVertexPoints[k].x;
            }
            if (testVertexPoints[k].y < minY) {
                minY = testVertexPoints[k].y;
            }
        }
        for(int k = 0; k < testVertexPoints.size(); k++) {
            testVertexPoints[k].x -= minX;
            testVertexPoints[k].y -= minY;
        }
        return testVertexPoints;
    }
    
    cv::Mat MatchTemplateToImage(cv::Mat searchImage, cv::Mat warpedTemplate)
    {
        int result_cols =  searchImage.cols - warpedTemplate.cols + 1;
        int result_rows = searchImage.rows - warpedTemplate.rows + 1;
        if (result_cols > 0 && result_rows > 0) {
            cv::Mat result;
            result.create( result_rows, result_cols, CV_32FC1 );
            
            double minVal; double maxVal;
            minMaxLoc(warpedTemplate, &minVal, &maxVal, 0, 0, cv::noArray());
            
            cv::Mat normSeatchROI;
            normalize(searchImage, normSeatchROI, minVal, maxVal, cv::NORM_MINMAX, -1, cv::Mat());
            /// Do the Matching and Normalize
            matchTemplate(normSeatchROI, warpedTemplate, result, match_method);
            return result;
        }
        else {
            //std::cout << "Results image too small" << std::endl;
            return cv::Mat();
        }
    }
    
    bool RunTemplateMatching(cv::Mat frame, int trackableId)
    {
        int templatePyrLevel = _trackables[trackableId]._templatePyrLevel;
        float scalefx = (float)_trackables[trackableId]._width / (float)_trackables[trackableId]._image[templatePyrLevel].cols;
        float scalefy = (float)_trackables[trackableId]._height / (float)_trackables[trackableId]._image[templatePyrLevel].rows;

        //std::cout << "Starting template match" << std::endl;
        std::vector<cv::Point2f> finalTemplatePoints, finalTemplateMatchPoints;
        //Get a handle on the corresponding points from current image and the marker
        std::vector<cv::Point2f> trackablePoints = _trackables[trackableId]._trackSelection[templatePyrLevel].GetTrackedFeatures();
        std::vector<cv::Point2f> trackablePointsWarped = _trackables[trackableId]._trackSelection[templatePyrLevel].GetTrackedFeaturesWarped(_trackables[trackableId]._homography);
        //Create an empty result image - May be able to pre-initialize this container
        
        int n = (int)trackablePointsWarped.size();
        if (_trackVizActive) {
            _trackViz.templateMatching = {};
            _trackViz.templateMatching.templateMatchingCandidateCount = n;
        }
        
        for (int j = 0; j < n; j++) {
            auto pt = trackablePointsWarped[j]; // In frame dimensions.
            if (cv::pointPolygonTest(_trackables[trackableId]._bBoxTransformed, trackablePointsWarped[j], true) > 0) {
                auto ptOrig = trackablePoints[j]; // In marker level 0 dimensions.
                
                cv::Rect templateSearchRoi = GetTemplateRoi(pt); // Where we are going to center our search for the template, in frame dimensions.
                cv::Rect frameROI(0, 0, frame.cols, frame.rows);
                if (IsRoiValidForFrame(frameROI, templateSearchRoi)) {
                    
                    // Calculate an upright rect region in the frame that minimally bounds the warped image of template we're searching for.
                    std::vector<cv::Point2f> vertexPoints = GetVerticesFromPoint(ptOrig, markerTemplateWidth << templatePyrLevel, markerTemplateWidth << templatePyrLevel); // In marker level 0 dimensions.
                    std::vector<cv::Point2f> vertexPointsResults;
                    perspectiveTransform(vertexPoints, vertexPointsResults, _trackables[trackableId]._homography);
                    cv::Rect srcBoundingBox = cv::boundingRect(cv::Mat(vertexPointsResults));
                    
                    // Now project that back into the marker level 0 image dimensions.
                    // Note that the projected vertices will contain at least the template, but typically
                    // also some back-projected area of the rect fitted around the warped template.
                    vertexPoints.clear();
                    vertexPoints = GetVerticesFromTopCorner(srcBoundingBox.x, srcBoundingBox.y, srcBoundingBox.width, srcBoundingBox.height);
                    perspectiveTransform(vertexPoints, vertexPointsResults, _trackables[trackableId]._homography.inv());
                    
                    // Work out the same vertices, but in the current pyramid level, rather than level 0.
                    std::vector<cv::Point2f> vertexPointsResultsTemplatePyrLevel;
                    for (auto& p : vertexPointsResults) {
                        vertexPointsResultsTemplatePyrLevel.push_back(cv::Point2f(p.x/ scalefx, p.y/ scalefy));
                    }
                    
                    // Find an homography that maps from the template to the search area in the image.
                    cv::Mat templateHomography;
                    {
                        std::vector<cv::Point2f> testVertexPoints = FloorVertexPoints(vertexPointsResultsTemplatePyrLevel); // In marker level templatePyrLevel dimensions.
                        std::vector<cv::Point2f> finalWarpPoints = GetVerticesFromTopCorner(0, 0, srcBoundingBox.width, srcBoundingBox.height); // In frame dimensions.
                        templateHomography = findHomography(testVertexPoints, finalWarpPoints, cv::RANSAC, ransac_thresh);
                    }
                    
                    if (!templateHomography.empty()) {
                        cv::Rect templateBoundingBox = cv::boundingRect(cv::Mat(vertexPointsResultsTemplatePyrLevel)); // In marker level templatePyrLevel dimensions.
                        cv::Rect searchROI = InflateRoi(templateSearchRoi, searchRadius);
                        if (IsRoiValidForFrame(frameROI, searchROI)) { // Make sure our search area falls within the frame.
                            cv::Rect markerRoi(0, 0, _trackables[trackableId]._image[templatePyrLevel].cols, _trackables[trackableId]._image[templatePyrLevel].rows);
                            templateBoundingBox = templateBoundingBox & markerRoi;
                            
                            if (templateBoundingBox.area() > 0 && searchROI.area() > templateBoundingBox.area()) {
                                cv::Mat searchImage = frame(searchROI);
                                cv::Mat templateImage = _trackables[trackableId]._image[templatePyrLevel](templateBoundingBox);
                                cv::Mat warpedTemplate;
                                
                                warpPerspective(templateImage, warpedTemplate, templateHomography, srcBoundingBox.size());
                                cv::Mat matchResult =  MatchTemplateToImage(searchImage, warpedTemplate);
                                
                                if (!matchResult.empty()) {
                                    double minVal; double maxVal;
                                    cv::Point minLoc, maxLoc, matchLoc;
                                    minMaxLoc( matchResult, &minVal, &maxVal, &minLoc, &maxLoc, cv::Mat() );
                                    if (minVal < 0.5) {
                                        matchLoc = minLoc;
                                        matchLoc.x += searchROI.x + (warpedTemplate.cols/2);
                                        matchLoc.y += searchROI.y + (warpedTemplate.rows/2);
                                        finalTemplatePoints.push_back(ptOrig);
                                        finalTemplateMatchPoints.push_back(matchLoc);
                                    } else {
                                        if (_trackVizActive) _trackViz.templateMatching.failedTemplateMinimumCorrelationCount++;
                                    }
                                } else {
                                    if (_trackVizActive) _trackViz.templateMatching.failedTemplateMatchCount++;
                                }
                            } else {
                                if (_trackVizActive) _trackViz.templateMatching.failedTemplateBigEnoughTestCount++;
                            }
                        } else {
                            if (_trackVizActive) _trackViz.templateMatching.failedSearchROIInFrameTestCount++;
                        }
                    } else {
                        if (_trackVizActive) _trackViz.templateMatching.failedGotHomogTestCount++;
                    }
                } else {
                    if (_trackVizActive) _trackViz.templateMatching.failedROIInFrameTestCount++;
                }
            } else {
                if (_trackVizActive) _trackViz.templateMatching.failedBoundsTestCount++;
            }
        }
        bool gotHomography = UpdateTrackableHomography(trackableId, finalTemplatePoints, finalTemplateMatchPoints);
        if (!gotHomography) {
            _trackables[trackableId]._isTracking = false;
            _trackables[trackableId]._isDetected = false;
            _currentlyTrackedMarkers--;
        }
        if (_trackVizActive) {
            _trackViz.templateMatching.templateMatchingOK = gotHomography;
            _trackViz.templateTrackablePoints = finalTemplatePoints;
            _trackViz.templateTrackedPoints = finalTemplateMatchPoints;
            //std::cout << "Template " << (gotHomography ? "PASS" : "FAIL") << ", candidates=" << _trackViz.templateMatching.templateMatchingCandidateCount
            //    << ", failedBoundsTest=" << _trackViz.templateMatching.failedBoundsTestCount
            //<< ", failedROIInFrameTest=" << _trackViz.templateMatching.failedROIInFrameTestCount
            //<< ", failedGotHomogTest=" << _trackViz.templateMatching.failedGotHomogTestCount
            //<< ", failedSearchROIInFrameTest=" << _trackViz.templateMatching.failedSearchROIInFrameTestCount
            //<< ", failedTemplateBigEnoughTest=" << _trackViz.templateMatching.failedTemplateBigEnoughTestCount
            //<< ", failedTemplateMatcht=" << _trackViz.templateMatching.failedTemplateMatchCount
            //<< ", failedTemplateMinimumCorrelation=" << _trackViz.templateMatching.failedTemplateMinimumCorrelationCount
            //<< std::endl;
        }
        return gotHomography;
    }
    
    /// Wrap raw frame data in `frame` with a cv::Mat structure, then process it for tracking.
    /// As the data is not copied,`frame` must remain valid for the duration of the call.
    void ProcessFrameData(unsigned char * frame)
    {
        cv::Mat newFrame(_frameSizeY, _frameSizeX, CV_8UC1, frame); // Via constructor cv::Mat(int rows, int cols, int type, void* data, size_t step=AUTO_STEP);
        ProcessFrame(newFrame);
        newFrame.release();
    }
    
    void ProcessFrame(cv::Mat frame)
    {
        //std::cout << "Building optical flow pyramid" << std::endl;
        cv::buildOpticalFlowPyramid(frame, _pyramid, winSize, k_OCVTOpticalFlowMaxPyrLevel);
        
        // Feature matching. Only do this phase if we're not already tracking the desired number of markers.
        if (_currentlyTrackedMarkers < _maxNumberOfMarkersToTrack) {
            cv::Mat detectionFrame;
            if (_featureDetectPyrLevel < 1) {
                detectionFrame = frame;
            } else {
                cv::Mat srcFrame = frame;
                for (int pyrLevel = 1; pyrLevel <= _featureDetectPyrLevel; pyrLevel++) {
                    cv::pyrDown(srcFrame, detectionFrame, cv::Size(0, 0));
                    srcFrame = detectionFrame;
                }
            }
            //std::cout << "Drawing detected markers to mask" << std::endl;
            cv::Mat featureMask = CreateFeatureMask(detectionFrame);
            //std::cout << "Detecting new features" << std::endl;
            std::vector<cv::KeyPoint> newFrameFeatures = _featureDetector.DetectFeatures(detectionFrame, featureMask);
            
            if (static_cast<int>(newFrameFeatures.size()) > minRequiredDetectedFeatures) {
                //std::cout << "Matching " << newFrameFeatures.size() << " new features" << std::endl;
                cv::Mat newFrameDescriptors = _featureDetector.CalcDescriptors(detectionFrame, newFrameFeatures);
                MatchFeatures(newFrameFeatures, newFrameDescriptors);
            }
        }
        
        // Optical flow and template matching. Only do this phase if something to track and we're not on the
        // very first frame.
        if (_trackVizActive) {
            _trackViz.opticalFlowTrackablePoints.clear();
            _trackViz.opticalFlowTrackedPoints.clear();
            _trackViz.opticalFlowOK = false;
        }
        if (_currentlyTrackedMarkers > 0) {
            //std::cout << "Begin tracking phase" << std::endl;
            for (int i = 0; i <_trackables.size(); i++) {
                if (_trackables[i]._isDetected) {
                    
                    // Calculate the ideal level in the pyramid at which to do template matching.
                    int templatePyrLevel = (int)log2f(1.0f / sqrtf((float)cv::determinant(_trackables[i]._homography)));
                    //std::cout << "templatePyrLevel=" << templatePyrLevel << " (scaleFactor=" << (1 << templatePyrLevel) << ")" << std::endl;
                    // Bound it by the levels we actually have available. Negative levels indicate a higher-res image would have been more appropriate.
                    if (templatePyrLevel < 0) templatePyrLevel = 0;
                    else if (templatePyrLevel > k_OCVTTemplateMatchingMaxPyrLevel) templatePyrLevel = k_OCVTTemplateMatchingMaxPyrLevel;
                    _trackables[i]._templatePyrLevel = templatePyrLevel;
                    if (_trackVizActive) _trackViz.templatePyrLevel = templatePyrLevel;
                    
                    std::vector<cv::Point2f> trackablePoints = _trackables[i]._trackSelection[templatePyrLevel].GetInitialFeatures();
                    std::vector<cv::Point2f> trackablePointsWarped = _trackables[i]._trackSelection[templatePyrLevel].GetTrackedFeaturesWarped(_trackables[i]._homography);
                    
                    if (_frameCount > 0 && _prevPyramid.size() > 0) {
                        //std::cout << "Starting Optical Flow" << std::endl;
                        if (!RunOpticalFlow(i, trackablePoints, trackablePointsWarped)) {
                            //std::cout << "Optical flow failed." << std::endl;
                        } else {
                            if (_trackVizActive) _trackViz.opticalFlowOK = true;
                            // Refine optical flow with template match.
                            if (!RunTemplateMatching(frame, i)) {
                                //std::cout << "Template matching failed." << std::endl;
                            }
                        }
                    }
                }
            }
        } else if (_trackVizActive) {
            memset(_trackViz.bounds, 0, 8*sizeof(float));
        }

        for (auto&& t : _trackables) {
            if (t._isDetected || t._isTracking) {
                
                std::vector<cv::Point2f> imgPoints = t._trackSelection[t._templatePyrLevel].GetTrackedFeaturesWarped(t._homography);
                std::vector<cv::Point3f> objPoints = t._trackSelection[t._templatePyrLevel].GetTrackedFeatures3d();
                
                CameraPoseFromPoints(t._pose, objPoints, imgPoints);
            }
        }
        
        // Done processing. Stash pyramid for optical flow for next frame.
        _pyramid.swap(_prevPyramid);
        _frameCount++;
    }
    
    void RemoveAllMarkers()
    {
        for (auto&& t : _trackables) {
            t.CleanUp();
        }
        _trackables.clear();
    }
    
    bool SaveTrackableDatabase(std::string fileName)
    {
        bool success = false;
        cv::FileStorage fs;
        fs.open(fileName, cv::FileStorage::WRITE);
        if (fs.isOpened())
        {
            try {
                int totalTrackables = (int)_trackables.size();
                fs << "totalTrackables" << totalTrackables;
                fs << "featureType" << (int)_selectedFeatureDetectorType;
                for (int i = 0; i <_trackables.size(); i++) {
                    std::string index = std::to_string(i);
                    fs << "trackableId" + index << _trackables[i]._id;
                    fs << "trackableFileName" + index << _trackables[i]._fileName;
                    fs << "trackableScale" + index << _trackables[i]._scale;
                    fs << "trackableImage" + index << _trackables[i]._image[0];
                    fs << "trackableWidth" + index << _trackables[i]._width;
                    fs << "trackableHeight" + index << _trackables[i]._height;
                    fs << "trackableDescriptors" + index << _trackables[i]._descriptors;
                    fs << "trackableFeaturePoints" + index << _trackables[i]._featurePoints;
                    fs << "trackableCornerPoints" + index << _trackables[i]._cornerPoints[0];
                }
                success = true;
            } catch (std::exception e) {
                ARLOGe("Error: Something went wrong while writing trackable database to path '%s'.\n", fileName.c_str());
            }
        }
        else
        {
            ARLOGe("Error: Could not create new trackable database at path '%s'.\n", fileName.c_str());
        }
        fs.release();
        return success;
    }
    
    bool LoadTrackableDatabase(std::string fileName)
    {
        bool success = false;
        cv::FileStorage fs;
        fs.open(fileName, cv::FileStorage::READ);

        if (fs.isOpened())
        {
            try {
                int numberOfTrackables = (int) fs["totalTrackables"];
                FeatureDetectorType featureType = defaultDetectorType;
                int featureTypeInt;
                fs["featureType"] >> featureTypeInt;
                featureType = (FeatureDetectorType)featureTypeInt;
                SetFeatureDetector(featureType);
                for(int i=0;i<numberOfTrackables; i++) {
                    TrackableInfo newTrackable;
                    std::string index = std::to_string(i);
                    fs["trackableId" + index] >> newTrackable._id;
                    fs["trackableFileName" + index] >> newTrackable._fileName;
                    fs["trackableScale" + index] >> newTrackable._scale;
                    fs["trackableImage" + index] >> newTrackable._image[0];
                    fs["trackableWidth" + index] >> newTrackable._width;
                    fs["trackableHeight" + index] >> newTrackable._height;
                    fs["trackableDescriptors" + index] >> newTrackable._descriptors;
                    fs["trackableFeaturePoints" + index] >> newTrackable._featurePoints;
                    fs["trackableCornerPoints" + index] >> newTrackable._cornerPoints[0];
                    newTrackable._bBox.push_back(cv::Point2f(0,0));
                    newTrackable._bBox.push_back(cv::Point2f(newTrackable._width, 0));
                    newTrackable._bBox.push_back(cv::Point2f(newTrackable._width, newTrackable._height));
                    newTrackable._bBox.push_back(cv::Point2f(0, newTrackable._height));
                    newTrackable._isTracking = false;
                    newTrackable._isDetected = false;
                    newTrackable._resetTracks = false;
                    for (int i = 0; i <= k_OCVTTemplateMatchingMaxPyrLevel; i++) {
                        if (i > 0) {
                            // For the base pyramid level, the image and Harris corners are read from the file.
                            // For other levels we need to generate them on the fly.
                            cv::pyrDown(newTrackable._image[i - 1], newTrackable._image[i]);
                            newTrackable._cornerPoints[i] = _harrisDetector.FindCorners(newTrackable._image[i]);
                        }
                        newTrackable._trackSelection[i] = TrackingPointSelector(newTrackable._cornerPoints[i], newTrackable._image[i].cols, newTrackable._image[i].rows, markerTemplateWidth, newTrackable._width, newTrackable._height);
                    }
                    _trackables.push_back(newTrackable);
                }
                success = true;
            } catch(std::exception e) {
                ARLOGe("Error: Something went wrong while reading trackable database from path '%s'.\n", fileName.c_str());
            }
        }
        else
        {
            ARLOGe("Error: Could not open trackable database from path '%s'.\n", fileName.c_str());
        }
        fs.release();
        return success;
    }
    
    void AddMarker(std::shared_ptr<unsigned char> buff, std::string fileName, int width, int height, int uid, float scale)
    {
        TrackableInfo newTrackable;
        // cv::Mat() wraps `buff` rather than copying it, but this is OK as we share ownership with caller via the shared_ptr.
        newTrackable._imageBuff = buff;
        newTrackable._image[0] = cv::Mat(height, width, CV_8UC1, buff.get());
        if (!newTrackable._image[0].empty()) {
            newTrackable._id = uid;
            newTrackable._fileName = fileName;
            newTrackable._scale = scale;
            newTrackable._width = newTrackable._image[0].cols;
            newTrackable._height = newTrackable._image[0].rows;
            newTrackable._featurePoints = _featureDetector.DetectFeatures(newTrackable._image[0], cv::Mat());
            newTrackable._descriptors = _featureDetector.CalcDescriptors(newTrackable._image[0], newTrackable._featurePoints);
            newTrackable._bBox.push_back(cv::Point2f(0,0));
            newTrackable._bBox.push_back(cv::Point2f(newTrackable._width, 0));
            newTrackable._bBox.push_back(cv::Point2f(newTrackable._width, newTrackable._height));
            newTrackable._bBox.push_back(cv::Point2f(0, newTrackable._height));
            newTrackable._isTracking = false;
            newTrackable._isDetected = false;
            newTrackable._resetTracks = false;
            for (int i = 0; i <= k_OCVTTemplateMatchingMaxPyrLevel; i++) {
                // We already have the image for the base pyramid level. Generate the others.
                if (i > 0) cv::pyrDown(newTrackable._image[i - 1], newTrackable._image[i]);
                newTrackable._cornerPoints[i] = _harrisDetector.FindCorners(newTrackable._image[i]);
                newTrackable._trackSelection[i] = TrackingPointSelector(newTrackable._cornerPoints[i], newTrackable._image[i].cols, newTrackable._image[i].rows, markerTemplateWidth, newTrackable._width, newTrackable._height);
            }
            
            _trackables.push_back(newTrackable);
            ARLOGi("2D marker added.\n");
        }
    }

    bool GetTrackablePose(int trackableId, float transMat[3][4])
    {
        if (!transMat) return false;
    
        auto t = std::find_if(_trackables.begin(), _trackables.end(), [&](const TrackableInfo& e) { return e._id == trackableId; });
        if (t != _trackables.end()) {
            if (t->_isDetected || t->_isTracking) {
                cv::Mat poseOut;
                t->_pose.convertTo(poseOut, CV_32FC1);
                //std::cout << "poseOut: " << poseOut << std::endl;
                memcpy(transMat, poseOut.ptr<float>(0), 3*4*sizeof(float));
                return true;
            }
        }
        return false;
    }
    
    bool IsTrackableVisible(int trackableId)
    {
        auto t = std::find_if(_trackables.begin(), _trackables.end(), [&](const TrackableInfo& e) { return e._id == trackableId; });
        if (t != _trackables.end()) {
            return (t->_isDetected || t->_isTracking);
        }
        return false;
    }
    
    void CameraPoseFromPoints(cv::Mat& pose, const std::vector<cv::Point3f>& objPts, const std::vector<cv::Point2f>& imgPts)
    {
        cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64FC1);          // output rotation vector
        cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64FC1);          // output translation vector
        
        cv::solvePnPRansac(objPts, imgPts, _K, _distortionCoeff, rvec, tvec);
        
        // Assemble pose matrix from rotation and translation vectors.
        cv::Mat rMat;
        Rodrigues(rvec, rMat);
        cv::hconcat(rMat, tvec, pose);
    }
    
    
    bool HasTrackables()
    {
        if (_trackables.size() > 0) {
            return true;
        }
        return false;
    }
    
    bool ChangeImageId(int prevId, int newId)
    {
        auto t = std::find_if(_trackables.begin(), _trackables.end(), [&](const TrackableInfo& e) { return e._id == prevId; });
        if (t != _trackables.end()) {
            t->_id = newId;
            return true;
        }
        return false;
    }

    std::vector<int> GetImageIds()
    {
        std::vector<int> imageIds;
        for (int i=0;i<_trackables.size(); i++) {
            imageIds.push_back(_trackables[i]._id);
        }
        return imageIds;
    }

    TrackedImageInfo GetTrackableImageInfo(int trackableId) const
    {
        TrackedImageInfo info;
        auto t = std::find_if(_trackables.begin(), _trackables.end(), [&](const TrackableInfo& e) { return e._id == trackableId; });
        if (t != _trackables.end()) {
            info.uid = t->_id;
            info.scale = t->_scale;
            info.fileName = t->_fileName;
            // Copy the image data and use a shared_ptr to refer to it.
            unsigned char *data = (unsigned char *)malloc(t->_width * t->_height);
            memcpy(data, t->_image[0].ptr(), t->_width * t->_height);
            info.imageData.reset(data, free); // Since we use malloc, pass `free` as the deallocator.
            info.width = t->_width;
            info.height = t->_height;
            info.fileName = t->_fileName;
        }
        return info;
    }
    
    void SetFeatureDetector(FeatureDetectorType detectorType)
    {
        _selectedFeatureDetectorType = detectorType;
        _featureDetector.SetFeatureDetector(detectorType);
    }

    FeatureDetectorType GetFeatureDetector(void) const
    {
        return _selectedFeatureDetectorType;
    }

    void SetMaximumNumberOfMarkersToTrack(int maximumNumberOfMarkersToTrack)
    {
        if (_maxNumberOfMarkersToTrack > 0) {
            _maxNumberOfMarkersToTrack = maximumNumberOfMarkersToTrack;
        }
    }

    int GetMaximumNumberOfMarkersToTrack(void) const
    {
        return _maxNumberOfMarkersToTrack;
    }
};

//
// Implementation wrapper.
//

PlanarTracker::PlanarTracker() : _trackerImpl(new PlanarTrackerImpl())
{
}

PlanarTracker::~PlanarTracker() = default;
PlanarTracker::PlanarTracker(PlanarTracker&&) = default;
PlanarTracker& PlanarTracker::operator=(PlanarTracker&&) = default;

void PlanarTracker::Initialise(ARParam cParam)
{
    _trackerImpl->Initialise(cParam);
}

void PlanarTracker::ProcessFrameData(unsigned char * frame)
{
    _trackerImpl->ProcessFrameData(frame);
}

void PlanarTracker::RemoveAllMarkers()
{
    _trackerImpl->RemoveAllMarkers();
}

void PlanarTracker::AddMarker(std::shared_ptr<unsigned char> buff, std::string fileName, int width, int height, int uid, float scale)
{
    _trackerImpl->AddMarker(buff, fileName, width, height, uid, scale);
}

bool PlanarTracker::GetTrackablePose(int trackableId, float transMat[3][4])
{
    return _trackerImpl->GetTrackablePose(trackableId, transMat);
}

bool PlanarTracker::IsTrackableVisible(int trackableId)
{
    return _trackerImpl->IsTrackableVisible(trackableId);
}

bool PlanarTracker::LoadTrackableDatabase(std::string fileName)
{
    return _trackerImpl->LoadTrackableDatabase(fileName);
}
bool PlanarTracker::SaveTrackableDatabase(std::string fileName)
{
    return _trackerImpl->SaveTrackableDatabase(fileName);
}

bool PlanarTracker::ChangeImageId(int prevId, int newId)
{
    return _trackerImpl->ChangeImageId(prevId, newId);
}
std::vector<int> PlanarTracker::GetImageIds()
{
    return _trackerImpl->GetImageIds();
}

TrackedImageInfo PlanarTracker::GetTrackableImageInfo(int trackableId)
{
    return _trackerImpl->GetTrackableImageInfo(trackableId);
}

void PlanarTracker::SetFeatureDetector(FeatureDetectorType detectorType)
{
    _trackerImpl->SetFeatureDetector(detectorType);
}

PlanarTracker::FeatureDetectorType PlanarTracker::GetFeatureDetector(void)
{
    return _trackerImpl->GetFeatureDetector();
}

void PlanarTracker::SetMinRequiredDetectedFeatures(int num)
{
    minRequiredDetectedFeatures = num; // OCVConfig
}

int PlanarTracker::GetMinRequiredDetectedFeatures(void)
{
    return minRequiredDetectedFeatures; // OCVConfig
}

void PlanarTracker::SetHomographyEstimationRANSACThreshold(double thresh)
{
    ransac_thresh = thresh; // OCVConfig
}

double PlanarTracker::GetHomographyEstimationRANSACThreshold(void)
{
    return ransac_thresh; // OCVConfig
}

void PlanarTracker::SetMaximumNumberOfMarkersToTrack(int maximumNumberOfMarkersToTrack)
{
    _trackerImpl->SetMaximumNumberOfMarkersToTrack(maximumNumberOfMarkersToTrack);
}

int PlanarTracker::GetMaximumNumberOfMarkersToTrack(void)
{
    return _trackerImpl->GetMaximumNumberOfMarkersToTrack();
}

void PlanarTracker::SetTrackerVisualizationActive(bool active)
{
    _trackerImpl->_trackVizActive = active;
    if (active) {
        _trackerImpl->_trackViz.reset();
    }
}

void *PlanarTracker::GetTrackerVisualization(void)
{
    return (void *)&_trackerImpl->_trackViz;
}
