// *****************************************************************************************
// Copyright (C) 2017 Juan C. SanMiguel and Andrea Cavallaro
// You may use, distribute and modify this code under the terms of the ACADEMIC PUBLIC
// license (see the attached LICENSE_WISE file).
//
// This file is part of the WiseMnet simulator
//
// Updated contact information:
//  - Juan C. SanMiguel - Universidad Autonoma of Madrid - juancarlos.sanmiguel@uam.es
//  - Andrea Cavallaro - Queen Mary University of London - a.cavallaro@qmul.ac.uk
//
// Please cite the following reference when publishing results obtained with WiseMnet:
//   J. SanMiguel & A. Cavallaro,
//   "Networked Computer Vision: the importance of a holistic simulator",
//   IEEE Computer, 50(7):35-43, Jul 2017, http://ieeexplore.ieee.org/document/7971873/
// *****************************************************************************************

/**
 * \file WiseMultiCameraPeriodicApp_MTIC_utils.cc
 * \author Juan C. SanMiguel (2017)
 * \brief Source file for the utilities of the MTIC tracker
 * \version 1.4
 */
#include "WiseMultiCameraPeriodicApp_MTIC_utils.h"

namespace mtic
{
    MTIC_t::MTIC_t() {
        init_MTIC_t(4, 2, 10, 10,0,1);
    }
    MTIC_t::MTIC_t(int _dimS, int _dimM, double _procNoiseCov, double _measNoiseCov, double _lamda, int _n_nodes) {
        init_MTIC_t(_dimS, _dimM, _procNoiseCov,_measNoiseCov,_lamda,_n_nodes);
    }
    MTIC_t::~MTIC_t()
    {
        dimS = -1;
    }

    /**
    * This function initializes the structure KalmanFilter_t with default values
    * @return The KF data
    */
    int MTIC_t::init_MTIC_t(int _dimS, int _dimM, double _procNoiseCov, double _measNoiseCov, double _lamda, int _n_nodes) {

       //MTIC_t icf;//create ICF filter
       dimS = _dimS;
       dimM = _dimM;
       n_nodes = _n_nodes;
       procNoiseCov = _procNoiseCov;
       measNoiseCov = _measNoiseCov;

       err    = -1;
       errAcc = 0;
       xgt      = cv::Mat::zeros(dimM, 1, CV_64F);
       z        = cv::Mat::zeros(dimM, 1, CV_64F);

       //prior, posterior and covariance of the state
       x_ = cv::Mat::zeros(dimS, 1, CV_64F); //prior state
       x  = cv::Mat::zeros(dimS, 1, CV_64F); //posterior state

       P = procNoiseCov*(cv::Mat_<double>(dimS, dimS) << 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1); //prior state covariance
       Pinv = P.inv(cv::DECOMP_SVD); //prior state covariance

       //transition matrix
       A = (cv::Mat_<double>(dimS, dimS) << 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1);
       cv::transpose(A,At);

       eyeS = cv::Mat::eye(dimS, dimS, CV_64F);

       //measurement matrix
       H = (cv::Mat_<double>(dimM, dimS) << 1, 0, 0, 0, 0, 1, 0, 0);
       cv::transpose(H,Ht);

       //process covariance
       Q = procNoiseCov*(cv::Mat_<double>(dimS, dimS) << 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.1, 0, 0, 0, 0, 0.1);

       //measurement covariance
       R = measNoiseCov*measNoiseCov* cv::Mat::eye(dimM, dimM, CV_64F);
       Rinv = R.inv(cv::DECOMP_SVD);

       //information-based variables
       J_ = cv::Mat::zeros(dimS, dimS, CV_64F); //prior information matrix
       J_ = eyeS * P.inv(cv::DECOMP_SVD);
       J = cv::Mat::zeros(dimS, dimS, CV_64F); //posterior information matrix
       v = cv::Mat::zeros(dimS, 1, CV_64F); //??
       V = cv::Mat::zeros(dimS, dimS, CV_64F); //??
       W = cv::Mat::zeros(dimS, dimS, CV_64F); //??

       u = cv::Mat::zeros(dimS, 1, CV_64F);    //information vector
       U = cv::Mat::zeros(dimS, dimS, CV_64F); //information matrix
       G = cv::Mat::zeros(dimS, dimS, CV_64F); //information matrix

       beta0 = 0;

       Pg = .9; //gate probability (you can change)
       lamda= _lamda; //clutter per sensor (you can change)
       double areafov = 30.0/360.0 * M_PI* 250.0*250.0;
       lamdaf = lamda/areafov;
       //lamdaf = lamda/500/500; //clutter per unit area

        //LOGGER << "ICF CONFIG" << endl << "A=" << endl << icf.A << endl << "H=" << endl << icf.H
        //        << endl << "P=" << endl << icf.P << endl << "Pinv=" << endl << icf.Pinv << endl
        //        << endl << "Rinv=" << endl << icf.Rinv << endl << "Q=" << endl << icf.Q << endl;
         /**/

       return 1;
    }

    int MTIC_t::initICFstate(cv::Mat _xgt, bool debug) {

       x_.at<double>(0) = xgt.at<double>(0);
       x_.at<double>(1) = xgt.at<double>(1);
       x_.at<double>(2) = 0;
       x_.at<double>(3) = 0;

       //initialize with some noise
       if(false)
       {
           x_.at<double>(0) = xgt.at<double>(0) + normal(0,0,sqrt(Q.at<double>(0,0)));
           x_.at<double>(1) = xgt.at<double>(1) + normal(0,0,sqrt(Q.at<double>(1,1)));
           x_.at<double>(2) = 0 + normal(0,0,sqrt(Q.at<double>(2,2)));
           x_.at<double>(3) = 0 + normal(0,0,sqrt(Q.at<double>(3,3)));
       }

       if (debug) {
           std::cout << " init x_=" <<  std::setprecision(3) << cv::format(mat2vec(x_), cv::Formatter::FMT_DEFAULT)
                << " xgt=" << cv::format(mat2vec(xgt), cv::Formatter::FMT_DEFAULT) << std::endl;
       }
       return 1;
    }

    /*!
    * Initialization of ICF for consensus (and also the first initial target position from the first sample).
    *
    * @param[in] tid Target ID of the associated ICF structures
    */
    void MTIC_t::prepData(bool detection_miss)
    {
      v = u + (1/(double)n_nodes * J_ + beta0 * U) * x_;
      V = (1/(double)n_nodes * J_) + U;/**/
      W = (1/(double)n_nodes * J_) + G;/**/
      //LOGGER <<"tid=" << tid<< " prepData: v=" << print(v,3) << " V=" << cv::sum(V).val[0] << " beta0=" << beta0 << endl;
      //LOGGER <<"tid=" << tid<< " prepData: u=" << print(u,3) << " U=" << cv::sum(U).val[0] << " G=" << cv::sum(G).val[0]<<" W=" << cv::sum(W).val[0]<< endl;
      // std::cout << "ERROR!!!" << std::endl;
    }

    void MTIC_t::update_state()
    {

    }

    void MTIC_t::update_state_neighbors(std::vector<mtic::node_ctrl_mtic_t::neigbourg_data_mtic_t> &data, double alpha, int n_neighbourgs, int max_neigb_network)
    {
      cv::Mat vtemp = cv::Mat::zeros(dimS, 1, cv::DataType<double>::type);    //information vector of node
      cv::Mat Vtemp = cv::Mat::zeros(dimS, dimS, cv::DataType<double>::type);    //information matrix of node
      cv::Mat Wtemp = cv::Mat::zeros(dimS, dimS, cv::DataType<double>::type);    //information vector of node

      std::vector<mtic::node_ctrl_mtic_t::neigbourg_data_mtic_t>::const_iterator n;
      for (n = data.begin(); n != data.end(); ++n)
         if (n->rcv_data == true)
         {
             vtemp = vtemp + n->v;
             Vtemp = Vtemp + n->V;
             Wtemp = Wtemp + n->W;
             //LOGGER <<"tid=" << tid<< " node=" << n->nodeID << ": v=" << vec2mat(n->v) << " V=" << cv::sum(n->V).val[0] << endl;
         }
      double Delta = (double)n_neighbourgs;
      //double eps = alpha/(double)max_neigb_network;
      double eps = alpha;

      v = (1-Delta*eps)*v + eps*vtemp;
      V = (1-Delta*eps)*V + eps*Vtemp;/**/
      W = (1-Delta*eps)*W + eps*Wtemp;/**/
      //LOGGER <<"tid=" << tid<< " own: v=" << vec2mat(icf.v) << " V=" << cv::sum(icf.V).val[0] << "(after update)"<< endl;
    }

    /*!
    * Function to estimate the final result after completing the consensus iterations
    *
    * @param[in] tid Target ID of the associated ICF structures
    */
    void MTIC_t::predict_state()
    {
     //prediction for next timestep
     x_ = A * x;

     // original equation "icf.eyeS/( ((icf.A/icf.J) * icf.At)+icf.Q);"
     // https://help.scilab.org/docs/5.5.2/en_US/slash.html
     // x=A/B => x*B=A. (right-matrix division or slash)
     // left-matrix division - X=A/B is equivalent to A*INV(B)
     P = A*J.inv(cv::DECOMP_SVD)*At+Q;

     //fix symmetry
     P.at<double>(1,0) = P.at<double>(0,1);
     P.at<double>(2,0) = P.at<double>(0,2);
     P.at<double>(3,0) = P.at<double>(0,3);
     P.at<double>(2,1) = P.at<double>(1,2);
     P.at<double>(3,1) = P.at<double>(1,3);
     P.at<double>(3,2) = P.at<double>(2,3);/**/

     // eye(p,p) / P
     Pinv = P.inv(cv::DECOMP_SVD);
     J_ = eyeS*Pinv;
     /**/
    }

    void MTIC_t::estimate_state()
    {
      // x= V \ v (left-matrix division or backslash)
      // According to http://es.mathworks.com/help/matlab/ref/mldivide.html
      //  x=A\B  => A*x=B => x = inv(A)*B
      x = V.inv(cv::DECOMP_SVD)*v;

      cv::Mat eigval;
      cv::eigen(W, eigval);

      bool negative=false;
      for (int i=0;i<eigval.rows*eigval.cols;i++)
          if (eigval.at<double>(i) < 0)
              negative = true;

      //J = n_nodes * V;
      if (negative==false)
      {
          W.copyTo(J);
          J=n_nodes*J;
      }
      else
          Pinv.copyTo(J);
    }

    void MTIC_t::estimate_and_predict_state()
    {
      estimate_state();
      predict_state();
    }

    double MTIC_t::compute_error()
    {
      //we only get estimated <xpos,ypos> to compute error
      cv::Mat aux1 = (cv::Mat_<double>(dimM, 1) <<  x.at<double>(0), x.at<double>(1));
      err =  cv::norm(aux1,xgt,cv::NORM_L2);

      if (std::isinf(err) != 0 || std::isnan(err) != 0)
          perror("Problem computing the estimation error (NaN or INF value)");

      return err;
    }

    node_ctrl_mtic_t::node_ctrl_mtic_t() {
      init_node_ctrl(-1,1,MAX_SIZE_BUFFER,4);
    }

    node_ctrl_mtic_t::node_ctrl_mtic_t(int _tid,int n_nodes,int buffersize,int dimS) {
      init_node_ctrl(_tid,n_nodes,buffersize,dimS);
    }

    int node_ctrl_mtic_t::init_node_ctrl(int _tid,int n_nodes,int buffersize, int dimS) {

        tid = _tid;
        initialized = false;
        first_start_time = -1;
        start_time = -1;
        detection_miss = false;

        //data for consensus iterations
        iter_counter = 0; // iteration of the consensus
        n_neighbourgs = -1; //#neigbours of the node - wait until the graphs are completed

        //buffer for neighbor's data of MAX_SIZE_BUFFER iterations
        nb_data_buffer.clear();
        nb_data_buffer.resize(buffersize);

        for (unsigned int j = 0; j < (unsigned int)buffersize; j++)
        {
          nb_data_buffer[j].nb_data.clear();
          nb_data_buffer[j].nb_data.resize(n_nodes);
          nb_data_buffer[j].iter_buffer=-1;
        }

        //buffer for neighbor's data of MAX_SIZE_BUFFER iterations
        for (int j=0; j<mtic::MAX_SIZE_BUFFER;j++){

           //fill neighbor data with zeros
           for (int n=0; n<n_nodes; n++){
               mtic::node_ctrl_mtic_t::neigbourg_data_mtic_t &nb_node = this->nb_data_buffer[j].nb_data[n]; //associated neigbour's data
               nb_node.rcv_data = false;  // data not received yet
               nb_node.v = cv::Mat::zeros(dimS, 1, cv::DataType<double>::type);    //information vector of node
               nb_node.V = cv::Mat::zeros(dimS, dimS, cv::DataType<double>::type);    //information vector of node
               //nb_node.W = cv::Mat::zeros(dimS, dimS, cv::DataType<double>::type);    //information vector of node
               nb_node.end_collaboration =false;
           }
        }
        return 1;
    }

    /*!
      * This function searches in the buffer for the index corresponding to a given target ID and iteration index.
      *
      * @param[in] tid Target ID
      * @param[in] iter_index Index of the consensus iteration
      * @return The index of the buffer with the data or the next available position to store data.
      *         Returns -1 if there are no available positions and there are no data related with the "iteration"
      */
     int node_ctrl_mtic_t::findIndexInBuffer(int iter_index)
     {
         int index=-1;

         //look for the index where the data of the iteration is stored in the buffer
         for (int i=0;i<mtic::MAX_SIZE_BUFFER;i++)
            if (nb_data_buffer[i].iter_buffer == iter_index) {
                index = i;
                break;
            }

         //If data is not found, look for the next available position in buffer
         if (index == -1)
             for (int i=0;i<mtic::MAX_SIZE_BUFFER;i++)
                 if (nb_data_buffer[i].iter_buffer==-1) {
                     index = i;
                     break;
                 }

         return index;
     }

     /*!
      * This function stores in the buffer the data for a given target ID, iteration index and neighbour node.
      *
      * @param[in] tid Target ID
      * @param[in] indBuf Index of the consensus iteration
      * @param[in] nodeID ID of the neighbour node
      *
      */
     int node_ctrl_mtic_t::storeDataInBuffer(int iter, int nodeID, WiseMultiCameraPeriodicApp_MTICPacket *m)
     {
         int indBuf = findIndexInBuffer(iter);

         if (indBuf == -1)
             return -1;

         //store data in the buffer
         nb_data_buffer[indBuf].iter_buffer = m->getIterationStep();
         nb_data_buffer[indBuf].nb_data[nodeID].rcv_data = true;
         nb_data_buffer[indBuf].nb_data[nodeID].tid = tid;
         nb_data_buffer[indBuf].nb_data[nodeID].nodeID = nodeID;
         m->getICFv().copyTo(nb_data_buffer[indBuf].nb_data[nodeID].v);
         m->getICFV().copyTo(nb_data_buffer[indBuf].nb_data[nodeID].V);
         m->getICFW().copyTo(nb_data_buffer[indBuf].nb_data[nodeID].W);

         return 1;
     }
}
