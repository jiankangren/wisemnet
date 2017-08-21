// *****************************************************************************
//  Copyright (C): Juan C. SanMiguel, 2015
//  Author(s): Juan C. SanMiguel
//
//  Developed at Queen Mary University of London (UK) & University Autonoma of Madrid (Spain)
//  This file is distributed under the terms in the attached LICENSE_2 file.
//
//  This file is part of the implementation for the people-detect-transmit demo:
//      J. SanMiguel & A. Cavallaro,
//      "Networked Computer Vision: the importance of a holistic simulator",
//      IEEE COMPUTER 2017, http://ieeexplore.ieee.org/document/XXXX/
//      Preprint available at http://www.eecs.qmul.ac.uk/~andrea/wise-mnet.html
//
//  Updated contact information:
//  - Juan C. SanMiguel - Universidad Autonoma of Madrid - juancarlos.sanmiguel@uam.es
//  - Andrea Cavallaro - Queen Mary University of London - a.cavallaro@qmul.ac.uk
//
// *****************************************************************************
#ifndef __WiseCameraPeopleDet_H__
#define __WiseCameraPeopleDet_H__

#include "WiseCameraPeriodicAlgo.h"
#include "WiseCameraPeopleDetPacket_m.h"
#include <opencv.hpp>


/*! \class WiseCameraPeopleDet
 *  \brief This class implements ...
 *
 *  Description...
 *
 */
class WiseCameraPeopleDet : public WiseCameraPeriodicAlgo
{
private:

private:
    static ofstream logger; //!< Used to collect node print-out
    std::string _testName;

    //Generic params
    int _mode;                  //!< 1-full frame 2-blob description
    bool _isSink;               //!< status of being server to RX data
    int _sinkNode;              //!< server to TX data

    //Detection params
    unsigned int _numDet;                //!< number of detected people in current frame
    cv::HOGDescriptor *_hog;    //!< HOG-based people detector
    std::vector<cv::Mat> _GKernelList; //!< List of kernels for Gabor Features
    bool _extractGabor;

    bool _show_results;

public:
    virtual ~WiseCameraPeopleDet();

protected:
    // Functions to be implemented from WiseCameraPeriodicAlgo class
    void at_startup();                      //!< Init internal variables. To implement in superclasses of WiseCameraPeriodicAlgo.
    void at_timer_fired(int index) {} ;     //!< Response to alarms generated by specific tracker. To implement in superclasses of WiseCameraPeriodicAlgo.
    void at_finishSpecific() {};
    bool at_init();                         //!< Init resources. To implement in superclasses of WiseCameraPeriodicAlgo.
    bool at_first_sample();                 //!< Operations at 1st example. To implement in superclasses of WiseCameraPeriodicAlgo.
    bool at_end_first_sample();             //!< Operations at the end of 1st example. To implement in superclasses of WiseCameraPeriodicAlgo.
    bool at_sample();                       //!< Operations at the >1st example. To implement in superclasses of WiseCameraPeriodicAlgo.
    bool at_end_sample();                   //!< Operations at the end of >1st example. To implement in superclasses of WiseCameraPeriodicAlgo.

    // Functions to be implemented from WiseBaseApplication class
    bool process_network_message(WiseApplicationPacket *); //!< Processing of packets received from network. To implement in superclasses of WiseBaseApplication.
    void handleDirectApplicationMessage(WiseApplicationPacket *); //!< Processing of packets received from network. To implement in superclasses of WiseBaseApplication.
    void make_measurements(const std::vector<WiseTargetDetection>&);  //!< Conversion of camera detections into ordered lists of measurements for tracking. To implement in superclasses of WiseBaseApplication.
    void handleMacControlMessage(cMessage *);

    void displayDescBGR(cv::Mat b_hist, cv::Mat g_hist, cv::Mat r_hist, int histSize, const string& winname);
};

#endif // __WiseCameraPeopleDet_H__