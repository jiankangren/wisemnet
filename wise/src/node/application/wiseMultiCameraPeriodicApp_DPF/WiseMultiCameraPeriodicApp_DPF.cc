// *****************************************************************************************
// Copyright (C) 2011  Christian Nastasi, 2017 Juan C. SanMiguel and Andrea Cavallaro
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
 * \file wiseMultiCameraPeriodicApp_DPF.h
 * \author Christian Nastasi (2011)
 * \author Juan C. SanMiguel (2017)
 * \brief Source file for the wiseMultiCameraPeriodicApp_DPF class
 * \version 1.4
 */


#include <wise/src/utils/WiseDebug.h> //for logs/debug (constant WISE_DEBUG_LEVEL must be defined first)
#include <wiseMultiCameraPeriodicApp_DPF/WiseMultiCameraPeriodicApp_DPF.h>
#include <wise/src/utils/WiseDebug.h> //for logs/debug (constant WISE_DEBUG_LEVEL must be defined first)
#include <wise/src/utils/WiseUtils.h>
#include "WiseException.h"
#include "pft.h"
//#include <set>

Define_Module(WiseMultiCameraPeriodicApp_DPF);

#define DPF_RNG_PARTICLE 	0
//#define WISE_DEBUG_32("WiseCamPerAppDPF:: trace() << "DPF: "

ofstream *WiseMultiCameraPeriodicApp_DPF::final_writer = NULL;
ofstream *WiseMultiCameraPeriodicApp_DPF::partial_writer = NULL;
vector<WiseMultiCameraPeriodicApp_DPF::rng_draw_t> WiseMultiCameraPeriodicApp_DPF::rng_draw;

static vector< set<string> > tracking_nodes; // tmp solution, be more elegant

void WiseMultiCameraPeriodicApp_DPF::at_finishSpecific()
{
	vector<WiseUtils::Gmm*>::iterator g = gmm_clusterers.begin();
	for (; g != gmm_clusterers.end(); ++g)
		delete *g;
	if (final_writer) {
		final_writer->close();
		delete final_writer;
		final_writer = NULL;
	}
	if (partial_writer) {
		partial_writer->close();
		delete partial_writer;
		partial_writer = NULL;
	}
}

/* ************************************************************************** */
/*                 EVENTS from (camera) simple periodic tracker               */
/* ************************************************************************** */
void WiseMultiCameraPeriodicApp_DPF::at_startup()
{
    WISE_DEBUG_32("WiseCamPerAppDPF::at_startup() called");

    //create output logs
	if (final_writer == NULL)
	{
        std::ostringstream os;
        os << base_out_path.c_str() << "DPF" << "_r" << std::setfill('0') << std::setw(4) << _currentRun << "_dpf_results.dat";
		final_writer = new ofstream( os.str().c_str() );
	}
	if (partial_writer == NULL)
	{
	    std::ostringstream os;
	    os << base_out_path.c_str() << "DPF" << "_r" << std::setfill('0') << std::setw(4) << _currentRun << "_dpf_part_results.dat";
	    partial_writer = new ofstream( os.str().c_str() );
	}

	//read input parameters
	n_particles = par("n_particles");
	trace_particles = par("trace_particles");
	particle_spread = par("particle_spreading_factor");
	n_gmm_components = par("n_gmm_components");

	//get number of physical processes
	cModule *network = getParentModule()->getParentModule();
	n_targets = network->par("numPhysicalProcesses");
	init_structures(); 
	tracking_nodes.clear();
	tracking_nodes.resize(n_targets);
	if (firstNode()) 
		for (unsigned i = 0; i < n_targets; i++)
			tracking_nodes[i].clear();

	WISE_DEBUG_32("WiseCamPerAppDPF:: STARTUP : rng = " << getRNG(DPF_RNG_PARTICLE) << " N_particles = " << n_particles << " spreading_factor = " << particle_spread);
}

bool WiseMultiCameraPeriodicApp_DPF::at_init()
{
	WISE_DEBUG_32("WiseCamPerAppDPF::at_tracker_init() called");
	return true;
}

bool WiseMultiCameraPeriodicApp_DPF::at_sample()
{
    WISE_DEBUG_32("WiseCamPerAppDPF::at_tracker_sample() called");
    _tracking_step_counter++;

	for (unsigned tid = 0; tid < n_targets; tid++) {
		node_ctrl_t &ctrl = node_controls[tid];
		const string &z_str = target_measurements[tid].str();

		WISE_DEBUG_32("WiseCamPerAppDPF::NEW SAMPLE : z = " << (ctrl.detection_miss ? "MISS" : z_str));
		store_truth(tid);
		if (ctrl.target_lost) {
			if (!ctrl.detection_miss) {
				// Target was lost (or not found) but now we got it
				start_first_node_selection(tid);
			} else if (lastNode() && ctrl.initialized_once) {
				// Target was found (at least once) and lost, let's just log estimate (based on last PS) and truth
				const particle_set_t &ps = particle_sets[tid];
				estimate(tid, ps);
				log_partial_result(tid);
				log_final_result(tid);
			}
		} else if (ctrl.first_node) {
			ctrl.first_start_time = simTime().dbl();
			if (ctrl.very_first_node)
				ctrl.very_first_start_time = simTime().dbl();
			perform_first_step(tid);
			resMgr->consumeEnergy(this->EnergyPerProcessing/1000.0); //energy in Joules
            processingConsumedEnergy += this->EnergyPerProcessing/1000.0; //energy in Joules
		}
	}
	return true;
}

bool WiseMultiCameraPeriodicApp_DPF::at_end_sample()
{
	WISE_DEBUG_32("WiseCamPerAppDPF::at_tracker_end_sample() called");
	for (unsigned tid = 0; tid < n_targets; tid++) {
		const node_ctrl_t &ctrl = node_controls[tid];
		if (ctrl.target_lost && ctrl.first_node_candidate)
			complete_first_node_selection(tid);
	}
	return true;
}

void WiseMultiCameraPeriodicApp_DPF::make_measurements(const vector<WiseTargetDetection>&dt)
{
    WISE_DEBUG_32("WiseCamPerAppDPF::make_measurements() called");
	unsigned tid = 0;

	vector<const WiseTargetDetection*> ordered;
	ordered.resize(n_targets, NULL);
	
	for(vector<WiseTargetDetection>::const_iterator d = dt.begin(); d != dt.end(); ++d)
		ordered[d->target_id] = &(*d);
	
	for (; tid < n_targets && tid < dt.size(); tid++) {
		WISE_DEBUG_32("WiseCamPerAppDPF::Make Measurement TID=" << tid);
		node_ctrl_t &ctrl = node_controls[tid];
		state_t_2D &x0 = init_states[tid];
		measurement_t_2D &z = target_measurements[tid];

		if (ordered[tid] == NULL) {
			ctrl.detection_miss = true;
			continue;
		}
		const WiseTargetDetection &d = *(ordered[tid]);
		if (!d.valid) {
			ctrl.detection_miss = true;
			continue;
		}
		if (!ctrl.initialized) {
			x0.x = (d.true_bb_x_tl + d.true_bb_x_br) / 2;
			x0.y = (d.true_bb_y_tl + d.true_bb_y_br) / 2;
			WISE_DEBUG_32("WiseCamPerAppDPF:: Read Initial TRUE state");
		}
		z.x = (d.ext_bb_x_tl + d.ext_bb_x_br) / 2;
		z.y = (d.ext_bb_y_tl + d.ext_bb_y_br) / 2;
		ctrl.detection_miss = false;
	}
	// Set the residuals to detection_miss
	for (; tid < n_targets; tid++) {
		node_ctrl_t &ctrl = node_controls[tid];
		ctrl.detection_miss = true;
	}
} 

bool WiseMultiCameraPeriodicApp_DPF::process_network_message(WiseBaseAppPacket *pkt)
{
    WISE_DEBUG_32("WiseCamPerAppDPF::process_network_message() called");

    WiseMultiCameraPeriodicApp_DPFPacket *m = check_and_cast<WiseMultiCameraPeriodicApp_DPFPacket*>(pkt);
	unsigned tid = m->getTargetID();

	node_ctrl_t &ctrl = node_controls[tid];
	particle_set_t &ps = particle_sets[tid];
	gmm_t &gmm = gmm_mixtures[tid];

	ctrl.first_start_time = m->getFirstStartTime();
	ctrl.very_first_start_time = m->getVeryFirstStartTime();

	ctrl.target_lost = m->getTargetLost();
	if (ctrl.target_lost) {
		WISE_DEBUG_32("WiseCamPerAppDPF::RECEIVED: target LOST msg");
		ctrl.initialized = false;
		// TODO: VERY VERY TEMPORARY, find a more elegant solution!
		ps = *((particle_set_t*)m->getFakeVoidPointer());
		return false;
	}
	if (ctrl.first_node) {
		throw WiseException("First node SHOULD NOT receive messages in the DPF algorithm implementation");
	}
	if (!ctrl.initialized)
		ctrl.initialized = true;
	ctrl.aggregation_step = m->getAggregationStep();
	ctrl.first_step_failed = m->getFirstStepFailed();
	if (m->getUseGmm()) {
		ctrl.use_gmm_input = true;
		gmm = m->gmm;
		vector<state_t_2D>::iterator p = ps.particles.begin();
		vector<double>::iterator w = ps.weights.begin();
		#ifdef PRINT_POSTERIORS
		ostringstream os;
		os << "posterior_gmm_k" << tracking_step_counter;
		ofstream f(os.str().c_str(), ios_base::app);
		#endif
		for (; p != ps.particles.end(); ++p, ++w) { 
			draw_from_gmm(gmm, *p);
			*w = 1 / (double)n_particles;
			//*w = 0;
			#ifdef PRINT_POSTERIORS
			if (tid == 0) {
				f << node_controls[0].aggregation_step << "  " << self << "   " << *w << "  " << p->x << "  " << p->y << endl;
			}
			#endif
		}
		WISE_DEBUG_32("WiseCamPerAppDPF:: RECEIVED: GMM of PP: components=" << m->getGmmArraySize());
	} else {
		ctrl.use_gmm_input = false;
		ps = *((particle_set_t*)m->getFakeVoidPointer());
		WISE_DEBUG_32("WiseCamPerAppDPF:: RECEIVED: ParticleSet of PP: fake_void_ptr=" << hex << m->getFakeVoidPointer());
	}

	if (ctrl.first_step_failed) {
		ctrl.first_node = true;
		ctrl.first_start_time = simTime().dbl();
		perform_first_step(tid);
		resMgr->consumeEnergy(this->EnergyPerProcessing/1000.0); //energy in Joules
        processingConsumedEnergy += this->EnergyPerProcessing/1000.0; //energy in Joules
	} else {
		perform_intermediate_step(tid);
		resMgr->consumeEnergy(this->EnergyPerProcessing/1000.0); //energy in Joules
        processingConsumedEnergy += this->EnergyPerProcessing/1000.0; //energy in Joules
	}


	return true;
} 

void WiseMultiCameraPeriodicApp_DPF::handleDirectApplicationMessage(WiseBaseAppPacket *pkt)
{
    WISE_DEBUG_32("WiseCamPerAppDPF::handleDirectApplicationMessage() called");

    WiseMultiCameraPeriodicApp_DPFPacket *m = check_and_cast<WiseMultiCameraPeriodicApp_DPFPacket*>(pkt);
	unsigned tid = m->getTargetID();
	node_ctrl_t &ctrl = node_controls[tid];

	if (m->getFirstNodeSelectionCompleted()) {
		ctrl.aggregation_step = 0;
		ctrl.target_lost = false;
		ctrl.initialized = true;
		ctrl.initialized_once = true;
		ctrl.first_node_candidate = false;
		ctrl.first_node_candidates.clear();
		return;
	}

	if (!m->getFirstNodeSelection()) {
		WISE_DEBUG_32("WiseCamPerAppDPF:: RECV DIRECT APP: WEIRD: unexpected");
		return; // If so, why am I getting an unexpected directApp msg?
	}
	string id = m->getApplicationInteractionControl().source.c_str();
	double score = m->getFirstNodeSelectionScore();
	ctrl.first_node_candidates.insert(pair<double, string>(score, id));
	WISE_DEBUG_32("WiseCamPerAppDPF:: RECV DIRECT APP: add first node candidate " << id << "  score" << score);
}

/* ************************************************************************** */
/*                      DPF Algorithm main functions                          */
/* ************************************************************************** */
void WiseMultiCameraPeriodicApp_DPF::start_first_node_selection(unsigned tid)
{
    WiseMultiCameraPeriodicApp_DPFPacket *pkt;
	node_ctrl_t &ctrl = node_controls[tid];
	const WiseTargetDetection &d = _buf.detections[tid];
	state_t_2D &x0 = init_states[tid];

	x0.x = (d.true_bb_x_tl + d.true_bb_x_br) / 2;
	x0.y = (d.true_bb_y_tl + d.true_bb_y_br) / 2;
	double s = visibility_score(x0, _cam_info);
	ctrl.first_node_candidate = true;

	pkt = new WiseMultiCameraPeriodicApp_DPFPacket("Wise DPF Packet", APPLICATION_PACKET);
	// Note: packet size should be properly set when network messages will be used instead of DirectApplication msg
	// unsigned pkt_size = 1;
	// pkt->setByteLength(pkt_size); 

	pkt->setTargetID(tid);
	pkt->setDetectionMiss(ctrl.detection_miss);
	pkt->setTargetLost(true);
	pkt->setFirstNodeSelection(true);
	pkt->setFirstNodeSelectionScore(s);

	ctrl.first_node_candidates.insert(pair<double, string>(s, selfAddress));

	WISE_DEBUG_32("WiseCamPerAppDPF::SEND DIRECT APP: first node select: dst=ALL score=" << s);
	sendDirectApplicationMessage(pkt, "-1");
}

void WiseMultiCameraPeriodicApp_DPF::complete_first_node_selection(unsigned tid)
{
	node_ctrl_t &ctrl = node_controls[tid];
	particle_set_t &ps = particle_sets[tid];
	WISE_DEBUG_32("WiseCamPerAppDPF::FIST NODE SELECTED:  " << ctrl.first_node_candidates.begin()->second << " score " << ctrl.first_node_candidates.begin()->first);
	if (ctrl.first_node_candidates.begin()->second != selfAddress) 
		return; // This node is not selected (wait for completed msg)

	// If the current node is the selected one:
	//   1) initialize tracker
	//   2) send 'completed' message to the other nodes

	// Initialization: from ground truth or from last estimated state?
	#define DPF_REINIT_FROM_TRUTH
	#ifdef DPF_REINIT_FROM_TRUTH
	ctrl.initialized_once = true;
	draw_initial_particles(tid, ps);
	#else
	if (!ctrl.initialized_once) {
		ctrl.initialized_once = true;
		draw_initial_particles(tid, ps);
	} 
	#endif
	ctrl.first_node = true;
	ctrl.first_start_time = simTime().dbl();
	ctrl.very_first_node = true;
	ctrl.very_first_start_time = simTime().dbl();
	ctrl.aggregation_step = 0;
	ctrl.target_lost = false;
	ctrl.initialized = true;
	ctrl.first_node_candidate = false;
	ctrl.first_node_candidates.clear();

	// Note: packet size should be properly set when network messages will be used instead of DirectApplication msg
	// unsigned pkt_size = 1;
	// pkt->setByteLength(pkt_size); 

	WiseMultiCameraPeriodicApp_DPFPacket *pkt;
	pkt = new WiseMultiCameraPeriodicApp_DPFPacket("Wise DPF Packet", APPLICATION_PACKET);
	pkt->setTargetID(tid);
	pkt->setFirstNodeSelection(false);
	pkt->setFirstNodeSelectionCompleted(true);

	WISE_DEBUG_32("WiseCamPerAppDPF::SEND DIRECT APP: first node selected: " << selfAddress);
	sendDirectApplicationMessage(pkt, "-1");
}

void WiseMultiCameraPeriodicApp_DPF::perform_first_step(unsigned tid)
{
	WISE_DEBUG_32("WiseCamPerAppDPF:: DPF Performing FIRST STEP (step=" << _tracking_step_counter << ")");

	particle_set_t &ps = particle_sets[tid];
	const measurement_t_2D &z = target_measurements[tid];
	node_ctrl_t &ctrl = node_controls[tid];

	ctrl.aggregation_step = 0;
	ctrl.target_lost = false;

	if (!ctrl.detection_miss) {
		resampling(ps);
		predict_and_update(ps, z);
		ctrl.first_step_failed = false;
	} else {
		WISE_DEBUG_32("WiseCamPerAppDPF::First node missing detect: start iteration fails");
		ctrl.first_step_failed = true;
	}

	estimate(tid, ps);  // Just the estimate at the first node
	log_partial_result(tid);
	select_next_node(tid);

        if (!ctrl.last_node && !ctrl.first_step_failed) {
		// Everything is fine, send info to next node
		WISE_DEBUG_32("WiseCamPerAppDPF:: Send to next node");
		send_information(tid);
		ctrl.first_node = false;
                return;
	} else if (!ctrl.last_node && ctrl.first_step_failed) {
		WISE_DEBUG_32("WiseCamPerAppDPF:: Try other nodes that could start iteration");
		// There are other nodes to be included in the iteration step
		send_information(tid);
		ctrl.first_node = false;
		ctrl.very_first_node = false;
		return;
	} else if (ctrl.last_node && !ctrl.first_step_failed) { 
		WISE_DEBUG_32("WiseCamPerAppDPF::First node is Last node: single node estimation");
		// This is the last node in the iteration
		perform_last_step(tid);
		return;
	}
	// All the nodes (included the current one) in the overlapping-FOV neighbourhood failed to detect the target.
	ctrl.target_lost = true;
	ctrl.initialized = false;
	send_target_lost(tid);
	log_final_result(tid); // Log information
	ctrl.first_node = false;
	ctrl.very_first_node = false;
	ctrl.last_node = false;
}

void WiseMultiCameraPeriodicApp_DPF::perform_intermediate_step(unsigned tid)
{
	WISE_DEBUG_32("WiseCamPerAppDPF:: DPF Performing n-STEP (step=" << _tracking_step_counter << ")");

	particle_set_t &ps = particle_sets[tid];
	const measurement_t_2D &z = target_measurements[tid];
	node_ctrl_t &ctrl = node_controls[tid];

	ctrl.aggregation_step++;
	ctrl.target_lost = false;

	if (!ctrl.detection_miss) 
		importance_sampling(ps, z);
	estimate(tid, ps);  // Intermediate (and eventually last) estimate
	log_partial_result(tid);
	select_next_node(tid);
	if (ctrl.last_node)
		perform_last_step(tid);
	else
		send_information(tid);
}

void WiseMultiCameraPeriodicApp_DPF::perform_last_step(unsigned tid)
{
	WISE_DEBUG_32("WiseCamPerAppDPF:: DPF Performing LAST STEP (step=" << _tracking_step_counter << ")");

	node_ctrl_t &ctrl = node_controls[tid];

	log_final_result(tid);
	tracking_nodes[tid].clear();
	ctrl.last_node = false;
	ctrl.first_node = true;
	ctrl.very_first_node = true;
}

#ifndef DPF_NEXT_SELECT_MODE 
#define DPF_NEXT_SELECT_MODE 3
#endif

void WiseMultiCameraPeriodicApp_DPF::select_next_node(unsigned tid)
{
	// The 'tracking_nodes' set is used to check if the nodes has been
	// already used in the aggregation chain.
	// Note: this is a tmp solution (uses global memory!), probably a message exchange should be used instead.

	node_ctrl_t &ctrl = node_controls[tid];
	if (ctrl.first_node)
		tracking_nodes[tid].insert(selfAddress);

        #if (DPF_NEXT_SELECT_MODE > 0) && (DPF_NEXT_SELECT_MODE < 2)
	// MODE 1 
	// selecting using always the same order of preference [0 -> N]
	vector<neighbour_cam_t>::const_iterator n;
	n = neighborsFOV.begin();
	for (; n != neighborsFOV.end(); ++n) {
		pair<set<string>::iterator, bool> ret;
		ret = tracking_nodes[tid].insert(n->node_id);
		if (ret.second) {
			ctrl.next_node = n->node_id;
			return;
		}
	}
	ctrl.last_node = true;

        #elif (DPF_NEXT_SELECT_MODE > 1) && (DPF_NEXT_SELECT_MODE < 3)
	// MODE 2 
	// selecting using the order of preference [best -> worst]
	// where ...
	//   best : camera closest to the estimated (state) position;
	//   worst: camera farthest from the estimated position.
	//
	// NOTE
	// The last node of the chain is always the worst node.
	// The first node is always the last at the previous steps.
	// Consequentely, the 2 worst nodes end up to be the first/last nodes.
	// Moreover, the second node is always the best node.
	//
	// Consideration: we could change the priority so that the last/first 
	// nodes are selected to be the best.
	multimap<double, string> order_nodes;
	vector<neighbour_cam_t>::const_iterator n;
	n = neighborsFOV.begin();
	ostringstream os;
	os << "[";
	for (; n != neighborsFOV.end(); ++n) {
		double cx, cy, cz;
		n->cam_info.get_position(cx, cy, cz);
		const state_t &x_pred = target_states[tid];
		cx -= x_pred.x;
		cy -= x_pred.y;
		double score = sqrt((cx * cx) + (cy * cy));
		os << " n_" << n->node_id << "#" << score;
		order_nodes.insert(pair<double, string>(score, n->node_id));
	}
	os << " ]";
	multimap<double, string>::const_iterator m;
	for (m = order_nodes.begin(); m != order_nodes.end(); ++m) {
		pair<set<string>::iterator, bool> ret;
		ret = tracking_nodes[tid].insert(m->second);
		if (ret.second) {
			os << " -> next = " << m->second << "#" << m->first;
			WISE_DEBUG_32("WiseCamPerAppDPF:: Next Node Selection: " << os.str());
			ctrl.next_node = m->second;
			return;
		}
	}
	ctrl.last_node = true;
	os << " -> this is LAST NODE";
	WISE_DEBUG_32("WiseCamPerAppDPF::Next Node Selection: " << os.str());

        #elif (DPF_NEXT_SELECT_MODE > 2) && (DPF_NEXT_SELECT_MODE < 4)
	// MODE 3 
	// same as MODE 2, i.e. selecting from best to worst, BUT in this case
	// a visibility test is applied. The nodes are selected among those that should
	// be seeing the target at the estimated (state) position.
	//
	// NOTE (chain of errors)
	// In this case the predicted position (at current node) is used to 
	// exclude some nodes from the iteration step, rather than sorting them
	// as in MODE 2. Because the estimated position changes according the 
	// current position in the aggregation chain, it is possible that, due
	// to highly uncorrect local prediction, some nodes will be not 
	// selected. And, in a bad scenario, those unselected nodes were indeed
	// capable of correcting the estimate. 
	//
	// Consideration: the order in the selection process has an impact on 
	// this problem; if the nodes is not certain about its prediction, then
	// it might be less selective (in the exclusion process)
	multimap<double, string> order_nodes;
	vector<neighbour_cam_t>::const_iterator n;
	n = _neighborsFOVoverlap.begin();
	ostringstream os;
	os << "[";
	for (; n != _neighborsFOVoverlap.end(); ++n) {
		const state_t_2D &x_pred = target_states[tid];
		double score = visibility_score(x_pred, n->cam_info);
		os << " n_" << n->node_id << "#" << score;
		if (!check_visibility(x_pred, n->cam_info))
			continue;
		os << "_VISIBLE ";
		order_nodes.insert(pair<double, string>(score, n->node_id));
	}
	os << " ]";
	multimap<double, string>::const_iterator m;
	for (m = order_nodes.begin(); m != order_nodes.end(); ++m) {
		pair<set<string>::iterator, bool> ret;
		ret = tracking_nodes[tid].insert(m->second);
		if (ret.second) {
			os << " -> next = " << m->second << "#" << m->first;
			WISE_DEBUG_32("WiseCamPerAppDPF:: Next Node Selection: " << os.str());
			ctrl.next_node = m->second;
			return;
		}
	}
	ctrl.last_node = true;
	os << " -> this is LAST NODE";
	WISE_DEBUG_32("WiseCamPerAppDPF::Next Node Selection: " << os.str());

	#else
	#error "Value for DPF_NEXT_SELECT_MODE is not VALID"
	#endif
}

double WiseMultiCameraPeriodicApp_DPF::visibility_score(const state_t_2D &s, const WiseCameraInfo& c, bool normalized) const
{
	double cx, cy, cz, max;

	c.get_position(cx, cy, cz);
	cx = cx - s.x;
	cy = cy - s.y;
	if (!normalized)
		return sqrt((cx * cx) + (cy * cy));

	WiseCameraInfo::fov_bb_t fov;
	c.get_fov_bb(fov);
	max = (fov.width / 2) * (fov.width / 2);
	max += (fov.height / 2) * (fov.height / 2);

	return sqrt(((cx * cx) + (cy * cy)) / max);
}

/* ************************************************************************** */
/*                         DPF Internal Functions                             */
/* ************************************************************************** */
void WiseMultiCameraPeriodicApp_DPF::init_structures()
{
	WISE_DEBUG_32("WiseCamPerAppDPF::init_structures() called");

	target_states.clear();
	target_measurements.clear();
	init_states.clear();
	particle_sets.clear();
	node_controls.clear();
	rng_draw.clear();
	ground_truths.clear();
	gmm_mixtures.clear();

	target_states.resize(n_targets);
	target_measurements.resize(n_targets);
	init_states.resize(n_targets);
	particle_sets.resize(n_targets);
	rng_draw.resize(n_particles);
	ground_truths.resize(n_targets);
	gmm_mixtures.resize(n_targets);
	for (unsigned i = 0; i < n_targets; i++) {
		node_ctrl_t info;
		info.initialized = false;
		info.initialized_once = false;
		info.first_node = false;
		info.very_first_node = false;
		info.last_node = false;
		info.detection_miss = true;
		info.target_lost = true;
		info.first_node_candidate = false;
		info.first_node_candidates.clear();
		node_controls.push_back(info);

		particle_sets[i].particles.clear();
		particle_sets[i].particles.resize(n_particles);
		particle_sets[i].weights.clear();
		particle_sets[i].weights.resize(n_particles);

		WiseUtils::Gmm *gmm = NULL;
		if (n_gmm_components >= 0) 
			gmm = new WiseUtils::Gmm(2, 20, n_gmm_components, true,
						 n_particles);
		gmm_clusterers.push_back(gmm);
	}

	flat_particles = new double[n_particles * 2];
}

void WiseMultiCameraPeriodicApp_DPF::draw_initial_particles(unsigned tid, particle_set_t &ps)
{
	state_t_2D x_0;
	x_0.x = init_states[tid].x;
	x_0.y = init_states[tid].y;
	double w_0 = 1 / ((double)n_particles);
	ps.particles.assign(n_particles, x_0);
	ps.weights.assign(n_particles, w_0);
}
	
void WiseMultiCameraPeriodicApp_DPF::importance_sampling(particle_set_t &ps, const measurement_t_2D &z)
{
	unsigned cnt = 1;
	ostringstream  *os = NULL;

	if (trace_particles) {
		os = new ostringstream();
		*os << "\nLikelihoods p(z_k|x_k) where z_k=" << z.str() << "\n";
	}

	double sum_new_w = 0;
	vector<state_t_2D>::iterator p = ps.particles.begin();
	vector<double>::iterator w = ps.weights.begin();
	for (; p != ps.particles.end(); ++p, ++w) {
		// Calculate likelihood and update weights
		double lh = likelihood(z, *p);
		*w = lh; //*w *= lh;
		sum_new_w += *w;
		if (trace_particles) {
			*os  << setw(10) << lh << " * "; 
			if (!(cnt++ % 5)) 
				*os << endl;
		}
	}

	unsigned idx = 0;
	if (trace_particles) {
		*os << "\nParticle Set: sum(w)=" << sum_new_w << "\n              x, y, w =\n\n";
		cnt = 1;
	}
	p = ps.particles.begin();
	w = ps.weights.begin();
	for (; p != ps.particles.end(); ++p, ++w) {
		if (sum_new_w != 0) 
			*w /= sum_new_w;
		else
			*w = 1 / (double) n_particles;
		idx++;
		if (trace_particles) {
			*os << setw(10) << p->x << "  " << setw(10) << 
			       p->y << "  " << setw(10) << *w << " * ";
			if (!(cnt++ % 5)) 
				*os << endl;
		}
	}
	if (trace_particles) {
		WISE_DEBUG_32("WiseCamPerAppDPF:: Importance Sampling (step=" << _tracking_step_counter << ") " << os->str());
		delete os;
	}
}

void WiseMultiCameraPeriodicApp_DPF::resampling(particle_set_t &ps)
{
	pft::particles_resampling<state_t_2D, double>(getRNG(0), ps.particles, ps.weights);

	if (trace_particles) {
		ostringstream os;
		unsigned cnt = 1;
		os << "\nParticle Set: x, y, w =\n\n";
		for (unsigned i = 0; i < ps.particles.size(); i++) {
			os << setw(10) << ps.particles[i].x << "  " << 
			      setw(10) << ps.particles[i].y << "  " << 
			      setw(10) << ps.weights[i] << " * ";
			if (!(cnt++ % 5)) 
				os << endl;
		}
		WISE_DEBUG_32("WiseCamPerAppDPF::Resampling (step=" << _tracking_step_counter << ") " << os.str());
	}
}
	
void WiseMultiCameraPeriodicApp_DPF::predict_and_update(particle_set_t &ps, const measurement_t_2D &z)
{
	double sum_new_w = 0;
	double n_p = (double)ps.particles.size();
	vector<state_t_2D>::iterator p = ps.particles.begin();
	vector<double>::iterator w = ps.weights.begin();
	for (; p != ps.particles.end(); ++p, ++w) {
		process_model(*p);
		*w = likelihood(z, *p);
		sum_new_w += *w;
	}

	if (sum_new_w != 0)
		for (w = ps.weights.begin(); w != ps.weights.end(); ++w)
			*w /= sum_new_w;
	else
		for (w = ps.weights.begin(); w != ps.weights.end(); ++w)
			*w = 1 / n_p;
	
	if (trace_particles) {
		ostringstream os;
		unsigned cnt = 1;
		os << "\nParticle Set: x, y, w =\n\n";
		for (unsigned i = 0; i < ps.particles.size(); i++) {
			os << setw(10) << ps.particles[i].x << "  " << 
			      setw(10) << ps.particles[i].y << "  " << 
			      setw(10) << ps.weights[i] << " * ";
			if (!(cnt++ % 5)) 
				os << endl;
		}
		WISE_DEBUG_32("WiseCamPerAppDPF::Pred & Update (step=" << _tracking_step_counter << ") " << os.str());
	}
}

void WiseMultiCameraPeriodicApp_DPF::estimate(unsigned tid, const particle_set_t &ps)
{
	state_t_2D &x_pred = target_states[tid];
	x_pred.x = 0; 
	x_pred.y = 0; 
	vector<state_t_2D>::const_iterator p = ps.particles.begin();
	vector<double>::const_iterator w = ps.weights.begin();
	#ifdef PRINT_POSTERIORS
	ostringstream os;
	os << "posterior_particleset_k" << tracking_step_counter;
	ofstream f(os.str().c_str(), ios_base::app);
	#endif
	for (; p != ps.particles.end(); ++p, ++w) {
		x_pred.x += *w * p->x;
		x_pred.y += *w * p->y;
		#ifdef PRINT_POSTERIORS
		if (tid == 0) {
			f << node_controls[0].aggregation_step << "  " << 
			     self << "   " << *w << "  " << p->x << "  " << 
			     p->y << endl;
		}
		#endif
	}
}

double WiseMultiCameraPeriodicApp_DPF::likelihood(const measurement_t_2D &z, const state_t_2D &x_p)
{
	// Note: a very simple likelihood function
	double dx = z.x - x_p.x;
	double dy = z.y - x_p.y;

	if (abs(dx) > 2 || abs(dy) > 2)
		return 0;
	return 1 / ((dx * dx) + (dy * dy) + 1);
}

void WiseMultiCameraPeriodicApp_DPF::process_model(state_t_2D &s)
{
	// Note: a very simple process 
	//
	// MAX_MOTION = V_m * T_s * spread
	//
	// V_m: velocity = D_m / T_m
	// D_m: space offset
	// T_m: motion period
	// T_s: sampling time
	// spread: particle_spreading_factor
	//
	// MAX_MOTION = (D_m / T_m) * T_s * spread = 
	//              D_m * (T_s / T_m) * spread
	//
	#define DPF_MAX_MOTION 	\
		(2 * ((double)_sampling_time / 20.0) * particle_spread)
	// TODO: remove the hard-coded values!

	double rx = uniform(-DPF_MAX_MOTION, DPF_MAX_MOTION);
	double ry = uniform(-DPF_MAX_MOTION, DPF_MAX_MOTION);
	s.x = s.x + rx;
	s.y = s.y + ry;
}

void WiseMultiCameraPeriodicApp_DPF::create_gmm(unsigned tid)
{
	const particle_set_t &ps = particle_sets[tid];
	WiseUtils::Gmm *clusterer = gmm_clusterers[tid];
	gmm_t &gmm = gmm_mixtures[tid];

	vector<state_t_2D>::const_iterator p = ps.particles.begin();
	double *d = flat_particles;
	for (; p != ps.particles.end(); ++p) {
		*(d++) = p->x;
		*(d++) = p->y;
	}
	clusterer->cluster(flat_particles, gmm);

	ostringstream os;
	gmm_t::const_iterator g = gmm.begin();
	unsigned k = 0;
	for (; g != gmm.end(); ++g) {
		os << "w_" << k << " = " << g->get_w() << "  "
		      "u_" << k << " = [" << g->get_mean(0) << 
		      ", " << g->get_mean(1) << "]  ";
		os << "C_" << k << " = [" << g->get_covar(0, 0) << ", " << 
		      g->get_covar(0, 1) << "; " << g->get_covar(1, 0) << 
		      ", " << g->get_covar(1, 1) << "]  " << endl;
		k++; 
	}
	WISE_DEBUG_32("WiseCamPerAppDPF::GMM :    M = " << gmm.size() << endl << os.str());
}

//static double normal_from_uniform(double m, double d, double u1, double u2)
//{
//    double a = 1.0 - u1;
//    double b = 1.0 - u2;
//   return m + d * sqrt(-2.0 * log(a)) * cos(PI * 2 * b);
//}

void WiseMultiCameraPeriodicApp_DPF::draw_from_gmm(const gmm_t &c, state_t_2D &s)
{
	unsigned i;

	s.x = 0;
	s.y = 0;
	if (c.size() == 0)
		return;
	// Assuming weights normalized!
	double rand_idx[c.size()];
	rand_idx[0] = c[0].get_w();
	for (i = 1; i < c.size(); i++) 
		rand_idx[i] = rand_idx[i - 1] + c[i].get_w();

	// Assuming independent axis
	double ux = uniform(0, 1);
	for (i = 0; i < c.size(); i++) 
		if (ux <= rand_idx[i])
			break;
	double mx = c[i].get_mean(0);
	double dx = c[i].get_covar(0, 0);
	s.x = normal(mx, dx);

	double uy = uniform(0, 1);
	for (i = 0; i < c.size(); i++) 
		if (uy <= rand_idx[i])
			break;
	double my = c[i].get_mean(1);
	double dy = c[i].get_covar(1, 1);
	s.y = normal(my, dy);
}

void WiseMultiCameraPeriodicApp_DPF::send_information(unsigned tid)
{
	node_ctrl_t &ctrl = node_controls[tid];
	particle_set_t &ps = particle_sets[tid];
	const gmm_t &gmm = gmm_mixtures[tid];
	WiseMultiCameraPeriodicApp_DPFPacket *pkt;
	unsigned pkt_size;

	pkt = new WiseMultiCameraPeriodicApp_DPFPacket("Wise DPF Packet", APPLICATION_PACKET);
	pkt->setTargetID(tid);
	pkt->setTrackingStep(_tracking_step_counter);
	pkt->setAggregationStep(ctrl.aggregation_step);
	pkt->setDetectionMiss(ctrl.detection_miss);
	pkt->setFirstStepFailed(ctrl.first_step_failed);
	pkt->setTargetLost(false);

	double t;
	t = ctrl.first_node ? simTime().dbl() : ctrl.first_start_time;
	pkt->setFirstStartTime(t);
	t = ctrl.very_first_node ? simTime().dbl() : ctrl.very_first_start_time;
	pkt->setVeryFirstStartTime(t);

	if (n_gmm_components < 0) {
		pkt->setUseGmm(false);
		resampling(ps);
		pkt->setFakeVoidPointer((unsigned long)&ps);
		pkt_size = (1 + 2) * sizeof(double) * n_particles;
	} else {
		resampling(ps);
		create_gmm(tid);
		pkt->setUseGmm(true);
		pkt->gmm = gmm; // using direct access to gmm member
		//pkt_size = (1 + 2 + 2) * sizeof(double) * par.size() * 1000;
		pkt_size = (1 + 2 + 2) * sizeof(double) * gmm.size();
	}

	pkt->setByteLength(pkt_size); 

	// Send to the specific destination node
	send_message(pkt, ctrl.next_node);
	WISE_DEBUG_32("WiseCamPerAppDPF::SEND Information: dst=" << ctrl.next_node << " len=" << pkt_size << " it=" << ctrl.aggregation_step <<
		     " miss=" << ctrl.detection_miss << " first_it_fail=" << ctrl.first_step_failed);
}

void WiseMultiCameraPeriodicApp_DPF::send_target_lost(unsigned tid)
{
	node_ctrl_t &ctrl = node_controls[tid];
	WiseMultiCameraPeriodicApp_DPFPacket *pkt;
	pkt = new WiseMultiCameraPeriodicApp_DPFPacket("Wise DPF Packet", APPLICATION_PACKET);
	pkt->setTargetID(tid);
	pkt->setTrackingStep(_tracking_step_counter);
	pkt->setAggregationStep(ctrl.aggregation_step);
	pkt->setDetectionMiss(ctrl.detection_miss);
	pkt->setFirstStepFailed(ctrl.first_step_failed);
	pkt->setTargetLost(true);

	double t;
	t = ctrl.first_node ? simTime().dbl() : ctrl.first_start_time;
	pkt->setFirstStartTime(t);
	t = ctrl.very_first_node ? simTime().dbl() : ctrl.very_first_start_time;
	pkt->setVeryFirstStartTime(t);

	// Right now we are not interested in considering the impact 
	// of the TARGET_LOST message, so we use a 1-byte packet size.
	unsigned pkt_size = 1;
	pkt->setByteLength(pkt_size); 

	// Send the current particle SET (entirely)
	// TODO: VERY VERY TEMPORARY, find a more elegant solution!
	pkt->setFakeVoidPointer((unsigned long)(&(particle_sets[tid])));
	// Send to all the nodes in the connectivity region (radio neighbours)
	send_message(pkt);
	WISE_DEBUG_32("WiseCamPerAppDPF::SEND Target LOST: dst=ALL len=" << pkt_size << " it=" << ctrl.aggregation_step <<
		     " miss=" << ctrl.detection_miss << " first_it_fail=" << ctrl.first_step_failed);
}

bool WiseMultiCameraPeriodicApp_DPF::check_visibility(const state_t_2D &s, const WiseCameraInfo &c) const
{
	// TODO: generalize to non-bounding-box FOV type
	WiseCameraInfo::fov_bb_t fov;
	c.get_fov_bb(fov);
	if (s.x < fov.min_x || s.x > fov.max_x)
		return false;
	if (s.y < fov.min_y || s.y > fov.max_y)
		return false;
	return true;
}

void WiseMultiCameraPeriodicApp_DPF::store_truth(unsigned tid)
{
	state_t_2D& truth = ground_truths[tid];
	if (_buf.detections.size() == 0 || _buf.detections.size() < tid + 1)
		return; 
	const WiseTargetDetection &d = _buf.detections[tid];
	truth.x = (d.true_bb_x_tl + d.true_bb_x_br) / 2;
	truth.y = (d.true_bb_y_tl + d.true_bb_y_br) / 2;
}

void WiseMultiCameraPeriodicApp_DPF::log_partial_result(unsigned tid)
{
	WISE_DEBUG_32("WiseCamPerAppDPF:: LOG __partial__ RESULTS (step=" << _tracking_step_counter << ")");
	static bool first_row = true;
	const state_t_2D &state = target_states[tid];
	const state_t_2D &truth = ground_truths[tid];
	const node_ctrl_t &ctrl = node_controls[tid];
	const char *sep = "  ";
	if (first_row) {
		*partial_writer << "k_step" << sep << "    t_sim" << sep << "nID" << sep << "pos" << sep << "  Ground Truth (x,y)" << sep <<
		        "  Prediction   (x,y)" << sep << "tID" << sep << "s0_OK" << sep << "DMiss" << sep << "TLost" << sep << "in_e2e_delay" << endl;
		first_row = false;
	}
	*partial_writer << setw(6) << _tracking_step_counter << sep;
	*partial_writer << setprecision(4) << setw(9) << simTime().dbl() << sep;
	*partial_writer << setw(3) << self << sep;
	*partial_writer << setw(3) << ctrl.aggregation_step << sep;
	*partial_writer << setprecision(4) << setw(9) << truth.x << sep << setprecision(4) << setw(9) << truth.y << sep;
	*partial_writer << setprecision(4) << setw(9) << state.x << sep << setprecision(4) << setw(9) << state.y << sep;
	*partial_writer << setw(3) << tid << sep;
	*partial_writer << setw(5) << (!ctrl.first_step_failed) << sep;
	*partial_writer << setw(5) << ctrl.detection_miss << sep;
	*partial_writer << setw(5) << ctrl.target_lost << sep;
	*partial_writer << setprecision(4) << setw(9) << last_packet_latency;
	*partial_writer << endl;
}

void WiseMultiCameraPeriodicApp_DPF::log_final_result(unsigned tid)
{
	WISE_DEBUG_32("WiseCamPerAppDPF::LOG RESULTS (step=" << _tracking_step_counter << ")");
	static bool first_row = true;
	const state_t_2D &state = target_states[tid];
	const state_t_2D &truth = ground_truths[tid];
	const node_ctrl_t &ctrl = node_controls[tid];
	const char *sep = "  ";
	if (first_row) {
		*final_writer << "k_step" << sep << "    t_sim" << sep << "nID" << sep << sep << "Ground Truth (x,y)" << sep << sep << "Prediction   (x,y)" << sep <<
				  "tID" << sep << "TLost" << sep << sep << "  delay_A" << sep << "  dalay_B" << endl;
		first_row = false;
	}
	double t_now = simTime().dbl();
	*final_writer << setw(6) << _tracking_step_counter << sep;
	*final_writer << setprecision(4) << setw(9) << t_now << sep;
	*final_writer << setw(3) << self << sep;
	*final_writer << setprecision(4) << setw(9) << truth.x << sep << setprecision(4) << setw(9) << truth.y << sep;
	*final_writer << setprecision(4) << setw(9) << state.x << sep << setprecision(4) << setw(9) << state.y << sep;
	*final_writer << setw(3) << tid << sep;
	*final_writer << setw(5) << ctrl.target_lost << sep;
	*final_writer << setprecision(4) << setw(9) << t_now - ctrl.first_start_time << sep;
	*final_writer << setprecision(4) << setw(9) << t_now - ctrl.very_first_start_time << sep;
	//*final_writer << setw(3) << expected_messages;
	*final_writer << endl;
}
