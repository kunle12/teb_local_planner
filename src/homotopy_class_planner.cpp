/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2015,
 *  TU Dortmund - Institute of Control Theory and Systems Engineering.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the institute nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Christoph Rösmann
 *********************************************************************/

#include <teb_local_planner/homotopy_class_planner.h>

namespace teb_local_planner
{
  
//!< Inline function used for calculateHSignature() in combination with VertexPose pointers   
inline std::complex<long double> getCplxFromVertexPosePtr(const VertexPose* pose)
{
  return std::complex<long double>(pose->x(), pose->y());
};

//!< Inline function used for calculateHSignature() in combination with HCP graph vertex descriptors
inline std::complex<long double> getCplxFromHcGraph(HcGraphVertexType vert_descriptor, const HcGraph& graph)
{
  return std::complex<long double>(graph[vert_descriptor].pos.x(), graph[vert_descriptor].pos.y());
};
  
//!< Inline function used for initializing the TEB in combination with HCP graph vertex descriptors
inline const Eigen::Vector2d& getVector2dFromHcGraph(HcGraphVertexType vert_descriptor, const HcGraph& graph)
{
  return graph[vert_descriptor].pos;
};
  




HomotopyClassPlanner::HomotopyClassPlanner() : cfg_(NULL), obstacles_(NULL), initialized_(false)
{
}
  
HomotopyClassPlanner::HomotopyClassPlanner(const TebConfig& cfg, ObstContainer* obstacles, TebVisualizationPtr visual)
{
  initialize(cfg, obstacles, visual);
}

HomotopyClassPlanner::~HomotopyClassPlanner()
{
}

void HomotopyClassPlanner::initialize(const TebConfig& cfg, ObstContainer* obstacles, TebVisualizationPtr visual)
{
  cfg_ = &cfg;
  obstacles_ = obstacles;
  initialized_ = true;
  
  setVisualization(visual);
}


void HomotopyClassPlanner::setVisualization(TebVisualizationPtr visualization)
{
  visualization_ = visualization;
}


 
bool HomotopyClassPlanner::plan(const std::vector<geometry_msgs::PoseStamped>& initial_plan, const geometry_msgs::Twist* start_vel, bool free_goal_vel)
{    
  ROS_ASSERT_MSG(initialized_, "Call initialize() first.");
  PoseSE2 start(initial_plan.front().pose.position.x, initial_plan.front().pose.position.y, tf::getYaw( initial_plan.front().pose.orientation) );
  PoseSE2 goal(initial_plan.back().pose.position.x, initial_plan.back().pose.position.y, tf::getYaw( initial_plan.back().pose.orientation) );
  Eigen::Vector2d vel = start_vel ?  Eigen::Vector2d( start_vel->linear.x, start_vel->angular.z ) : Eigen::Vector2d::Zero();
  return plan(start, goal, vel, free_goal_vel);
}


bool HomotopyClassPlanner::plan(const tf::Pose& start, const tf::Pose& goal, const geometry_msgs::Twist* start_vel, bool free_goal_vel)
{
  ROS_ASSERT_MSG(initialized_, "Call initialize() first.");
  PoseSE2 start_pose(start.getOrigin().getX(), start.getOrigin().getY(), tf::getYaw( start.getRotation() ) );
  PoseSE2 goal_pose(goal.getOrigin().getX(), goal.getOrigin().getY(), tf::getYaw( goal.getRotation() ) );
  Eigen::Vector2d vel = start_vel ?  Eigen::Vector2d( start_vel->linear.x, start_vel->angular.z ) : Eigen::Vector2d::Zero();
  return plan(start_pose, goal_pose, vel, free_goal_vel);
}

bool HomotopyClassPlanner::plan(const PoseSE2& start, const PoseSE2& goal, const Eigen::Vector2d& start_vel, bool free_goal_vel)
{	
  ROS_ASSERT_MSG(initialized_, "Call initialize() first.");
  
  // Update old TEBs with new start, goal and velocity
  updateAllTEBs(start, goal, start_vel);
  
  // Init new TEBs based on newly explored homotopy classes
  exploreHomotopyClassesAndInitTebs(start, goal, cfg_->obstacles.min_obstacle_dist, 0.1);
  // Optimize all trajectories in alternative homotopy classes
  optimizeAllTEBs(cfg_->optim.no_inner_iterations, cfg_->optim.no_outer_iterations);
  // Select which candidate (based on alternative homotopy classes) should be used
  selectBestTeb();
  // Delete any detours
  deleteTebDetours(0.0); 
  return true;
} 
 
Eigen::Vector2d HomotopyClassPlanner::getVelocityCommand() const
{
  TebOptimalPlannerConstPtr best_teb = bestTeb();
  if (!best_teb)
    return Eigen::Vector2d::Zero();
 
  return best_teb->getVelocityCommand(); 
}




void HomotopyClassPlanner::visualize()
{
  if (visualization_)
  {
    // Visualize graph
    if (cfg_->hcp.visualize_hc_graph)
      visualization_->publishGraph(graph_);
        
    // Visualize active tebs as marker
    visualization_->publishTebContainer(tebs_);
    
    // Visualize best teb
    TebOptimalPlannerConstPtr best_teb = bestTeb();
    if (best_teb)
      visualization_->publishLocalPlanAndPoses(best_teb->teb());
  }
  else ROS_DEBUG("Ignoring HomotopyClassPlanner::visualize() call, since no visualization class was instantiated before.");
}




void HomotopyClassPlanner::createGraph(const PoseSE2& start, const PoseSE2& goal, double dist_to_obst, bool limit_obstacle_heading)
{
  // Clear existing graph and paths
  clearGraph();
  
  // Direction-vector between start and goal and normal-vector:
  Eigen::Vector2d diff = goal.position()-start.position();
  
  if (diff.norm() < cfg_->goal_tolerance.xy_goal_tolerance) 
    return;
  
  Eigen::Vector2d normal(-diff[1],diff[0]); // normal-vector
  normal.normalize();
  normal = normal*dist_to_obst; // scale with obstacle_distance;
  
  // Insert Vertices
  HcGraphVertexType start_vtx = boost::add_vertex(graph_); // start vertex
  graph_[start_vtx].pos = start.position();
  diff.normalize();
  
  // store nearest obstacle keypoints -> only used if limit_obstacle_heading is enabled
  std::pair<HcGraphVertexType,HcGraphVertexType> nearest_obstacle; // both vertices are stored
  double min_dist = DBL_MAX;
  
  if (obstacles_!=NULL)
  {
    for (ObstContainer::const_iterator it_obst = obstacles_->begin(); it_obst != obstacles_->end(); ++it_obst)
    {
      // check if obstacle is placed in front of start point
      Eigen::Vector2d start2obst = (*it_obst)->getCentroid() - start.position();
      double dist = start2obst.norm();
      if (start2obst.dot(diff)/dist<0.1)
	continue;
      
      // Add Keypoints	
      HcGraphVertexType u = boost::add_vertex(graph_);
      graph_[u].pos = (*it_obst)->getCentroid() + normal;
      HcGraphVertexType v = boost::add_vertex(graph_);
      graph_[v].pos = (*it_obst)->getCentroid() - normal;
      
      // store nearest obstacle
      if (limit_obstacle_heading && dist<min_dist)
      {
	min_dist = dist;
	nearest_obstacle.first = u;
	nearest_obstacle.second = v;
      }
    }	
  }
  
  HcGraphVertexType goal_vtx = boost::add_vertex(graph_); // goal vertex
  graph_[goal_vtx].pos = goal.position();
  
  // Insert Edges
  HcGraphVertexIterator it_i, end_i, it_j, end_j;
  for (boost::tie(it_i,end_i) = boost::vertices(graph_); it_i!=end_i-1; ++it_i) // ignore goal in this loop
  {
    for (boost::tie(it_j,end_j) = boost::vertices(graph_); it_j!=end_j; ++it_j) // check all forward connections
    {
      if (it_i==it_j) 
	continue;
      // TODO: make use of knowing in which order obstacles are inserted and that for each obstacle 2 vertices are added,
      // therefore we must only check one of them.
      Eigen::Vector2d distij = graph_[*it_j].pos-graph_[*it_i].pos;
      distij.normalize();
      // Check if the direction is backwards:
      if (distij.dot(diff)<=cos(cfg_->hcp.obstacle_heading_threshold))
	continue;

			      
      // Check start angle to nearest obstacle 
      if (limit_obstacle_heading && *it_i==start_vtx && min_dist!=DBL_MAX)
      {
	if (*it_j == nearest_obstacle.first || *it_j == nearest_obstacle.second)
	{
	  Eigen::Vector2d keypoint_dist = graph_[*it_j].pos-start.position();
	  keypoint_dist.normalize();
	  Eigen::Vector2d start_orient_vec( cos(start.theta()), sin(start.theta()) ); // already normalized
	  // check angle
	  if (start_orient_vec.dot(keypoint_dist) < cos(cfg_->hcp.obstacle_heading_threshold)) 
	  {
	    ROS_DEBUG("createGraph() - deleted edge: limit_obstacle_heading");
	    continue;
	  }
	}
      }

      // Collision Check
      
      if (obstacles_!=NULL)
      {
	bool collision = false;
	for (ObstContainer::const_iterator it_obst = obstacles_->begin(); it_obst != obstacles_->end(); ++it_obst)
	{
	  if ( (*it_obst)->checkLineIntersection(graph_[*it_i].pos,graph_[*it_j].pos, 0.5*dist_to_obst) ) 
	  {
	    collision = true;
	    break;
	  }
	}
	if (collision) 
	  continue;
      }
      
      // Create Edge
      boost::add_edge(*it_i,*it_j,graph_);			
    }
  }
  
   
  // Find all paths between start and goal!
  std::vector<HcGraphVertexType> visited;
  visited.push_back(start_vtx);
  DepthFirst(graph_,visited,goal_vtx, start.theta(), goal.theta());
}


void HomotopyClassPlanner::createProbRoadmapGraph(const PoseSE2& start, const PoseSE2& goal, double dist_to_obst, bool limit_obstacle_heading)
{
  // Clear existing graph and paths
  clearGraph();
  
  // Direction-vector between start and goal and normal-vector:
  Eigen::Vector2d diff = goal.position()-start.position();
  double start_goal_dist = diff.norm();
  
  if (start_goal_dist<cfg_->goal_tolerance.xy_goal_tolerance) 
    return;
	  
  Eigen::Vector2d normal(-diff.coeffRef(1),diff.coeffRef(0)); // normal-vector
  normal.normalize();

  // Now sample vertices between start, goal and 5 m width to both sides
  // Let's start with a square area between start and goal (maybe change it later to something like a circle or whatever)
  
  double area_width = cfg_->hcp.roadmap_graph_area_width; // TODO param
    
  boost::random::uniform_real_distribution<double> distribution_x(0, start_goal_dist);  
  boost::random::uniform_real_distribution<double> distribution_y(0, area_width); 
  
  double phi = atan2(diff.coeffRef(1),diff.coeffRef(0)); // roate area by this angle
  Eigen::Rotation2D<double> rot_phi(phi);
  
  Eigen::Vector2d area_origin = start.position() - 0.5*area_width*normal; // bottom left corner of the origin
  
  // Insert Vertices
  HcGraphVertexType start_vtx = boost::add_vertex(graph_); // start vertex
  graph_[start_vtx].pos = start.position();
  diff.normalize();
  
  
  // Start sampling
  
  for (unsigned int i=0; i<cfg_->hcp.roadmap_graph_no_samples; ++i)
  {
  
    Eigen::Vector2d sample;
    bool coll_free;
    do // sample as long as a collision free sample is found
    {
      // Sample coordinates
      sample = area_origin + rot_phi*Eigen::Vector2d(distribution_x(rnd_generator_), distribution_y(rnd_generator_));
      
      // Test for collision
      coll_free = true;
      for (ObstContainer::const_iterator it_obst = obstacles_->begin(); it_obst != obstacles_->end(); ++it_obst)
      {
	if ( (*it_obst)->checkCollision(sample, dist_to_obst)) // TODO really keep dist_to_obst here?
	{
	  coll_free = false;
	  break;
	}
      }

    } while (!coll_free && ros::ok());
    
    // Add new vertex
    HcGraphVertexType v = boost::add_vertex(graph_);
    graph_[v].pos = sample;
  }
  
  // Now add goal vertex
  HcGraphVertexType goal_vtx = boost::add_vertex(graph_); // goal vertex
  graph_[goal_vtx].pos = goal.position();
  
  
  // Insert Edges
  HcGraphVertexIterator it_i, end_i, it_j, end_j;
  for (boost::tie(it_i,end_i) = boost::vertices(graph_); it_i!=end_i-1; ++it_i) // ignore goal in this loop
  {
    for (boost::tie(it_j,end_j) = boost::vertices(graph_); it_j!=end_j; ++it_j) // check all forward connections
    {
      if (it_i==it_j)
	continue;

      Eigen::Vector2d distij = graph_[*it_j].pos-graph_[*it_i].pos;
      distij.normalize();

      // Check if the direction is backwards:
      if (distij.dot(diff)<=cos(cfg_->hcp.obstacle_heading_threshold)) continue;
			      

      // Collision Check	
      bool collision = false;
      for (ObstContainer::const_iterator it_obst = obstacles_->begin(); it_obst != obstacles_->end(); ++it_obst)
      {
	if ( (*it_obst)->checkLineIntersection(graph_[*it_i].pos,graph_[*it_j].pos, 0.5*dist_to_obst) )
	{
	  collision = true;
	  break;
	}
      }
      if (collision)
	continue;
      
      // Create Edge
      boost::add_edge(*it_i,*it_j,graph_);			
    }
  }
  
  /// Find all paths between start and goal!
  std::vector<HcGraphVertexType> visited;
  visited.push_back(start_vtx);
  DepthFirst(graph_,visited,goal_vtx, start.theta(), goal.theta());
}


void HomotopyClassPlanner::DepthFirst(HcGraph& g, std::vector<HcGraphVertexType>& visited, const HcGraphVertexType& goal, double start_orientation, double goal_orientation)
{
  // see http://www.technical-recipes.com/2011/a-recursive-algorithm-to-find-all-paths-between-two-given-nodes/
  
  if ((int)tebs_.size() >= cfg_->hcp.max_number_classes)
    return; // We do not need to search for further possible alternative homotopy classes.
  
  HcGraphVertexType back = visited.back();

  /// Examine adjacent nodes
  HcGraphAdjecencyIterator it, end;
  for ( boost::tie(it,end) = boost::adjacent_vertices(back,g); it!=end; ++it)
  {
    if ( std::find(visited.begin(), visited.end(), *it)!=visited.end() )
      continue; // already visited

    if ( *it == goal ) // goal reached
    {
      visited.push_back(*it);
      
      
      // check H-Signature
      std::complex<long double> H = calculateHSignature(visited.begin(), visited.end(), boost::bind(getCplxFromHcGraph, _1, boost::cref(graph_)), obstacles_, cfg_->hcp.h_signature_prescaler);
      
      // check if H-Signature is already known
      // and init new TEB if no duplicate was found
      if ( addNewHSignatureIfNew(H, 0.1) )
      {
	addAndInitNewTeb(visited.begin(), visited.end(), boost::bind(getVector2dFromHcGraph, _1, boost::cref(graph_)), start_orientation, goal_orientation);
      }
      
      visited.pop_back();
      break;
    }
}

/// Recursion for all adjacent vertices
for ( boost::tie(it,end) = boost::adjacent_vertices(back,g); it!=end; ++it)
{
  if ( std::find(visited.begin(), visited.end(), *it)!=visited.end() || *it == goal)
    continue; // already visited || goal reached
  
  
  visited.push_back(*it);
  
  // recursion step
  DepthFirst(g, visited, goal, start_orientation, goal_orientation);
  
  visited.pop_back();
}
}
 


bool HomotopyClassPlanner::addNewHSignatureIfNew(const std::complex<long double>& H, double threshold)
{	  
  // iterate existing h-signatures and check if there is an existing H-Signature similar to the new one
  for (std::vector< std::complex<long double> >::const_iterator it = h_signatures_.begin(); it != h_signatures_.end(); ++it)
  {
    double diff_real = std::abs(it->real() - H.real());
    double diff_imag = std::abs(it->imag() - H.imag());
    if (diff_real<=threshold && diff_imag<=threshold)
      return false; // Found! Homotopy class already exists, therefore nothing added	
  }

  // Homotopy class not found -> Add to class-list, return that the h-signature is new
  h_signatures_.push_back(H);	 
  return true;
}
 
 

  
//! Small helper function to check whether two h-signatures are assumed to be equal. 
inline bool compareH( std::pair<TebOptPlannerContainer::iterator, std::complex<long double> > i, std::complex<long double> j ) {return std::abs(i.second.real()-j.real())<0.1 && std::abs(i.second.imag()-j.imag())<0.1;};
 

void HomotopyClassPlanner::renewAndAnalyzeOldTebs(bool delete_detours)
{
  // clear old h-signatures (since they could be changed due to new obstacle positions.
  h_signatures_.clear();

  // Collect h-signatures for all existing TEBs and store them together with the corresponding iterator / pointer:
  typedef std::vector< std::pair<TebOptPlannerContainer::iterator, std::complex<long double> > > TebCandidateType;
  TebCandidateType teb_candidates;

  // get new homotopy classes and delete multiple TEBs per homotopy class
  TebOptPlannerContainer::iterator it_teb = tebs_.begin();
  while(it_teb != tebs_.end())
  {
    // delete Detours if there is at least one other TEB candidate left in the container
    if (delete_detours &&  tebs_.size()>1 && it_teb->get()->teb().detectDetoursBackwards(cos(cfg_->hcp.obstacle_heading_threshold))) 
    {
      it_teb = tebs_.erase(it_teb); // delete candidate and set iterator to the next valid candidate
      continue;
    }
    
    // TEST: check if the following strategy performs well
    // if the obstacle region is really close to the TEB (far below the safty distance), the TEB will get heavily jabbed (pushed away) -> the obstacle error is very high!
    // Smoothing this effect takes a long time. Here we first detect this artefact and then initialize a new path from the homotopy-planner in renewHomotopyClassesAndInitNewTEB()!
    bool flag=false;
    for(ObstContainer::const_iterator it_obst = obstacles_->begin(); it_obst != obstacles_->end(); ++it_obst)
    {
      //TODO: findNearestBandpoint for arbitary obstacles
      double dist = it_obst->get()->getMinimumDistance( it_teb->get()->teb().Pose( it_teb->get()->teb().findClosestTrajectoryPose(it_obst->get()->getCentroid()) ).position() ); // TODO: inefficient. Can be stored in a previous loop
      if (dist < 0.03)
      {
	ROS_DEBUG("getAndFilterHomotopyClassesTEB() - TEB and Intersection Point are at the same place, erasing candidate.");	
	flag=true;
      }
    }
    if (flag)
    {
      it_teb = tebs_.erase(it_teb); // delete candidate and set iterator to the next valid candidate
      continue;
    }
			    
    // calculate H Signature for the current candidate
    std::complex<long double> H = calculateHSignature(it_teb->get()->teb().poses().begin(), it_teb->get()->teb().poses().end(), getCplxFromVertexPosePtr ,obstacles_, cfg_->hcp.h_signature_prescaler);

    teb_candidates.push_back(std::make_pair(it_teb,H));
    ++it_teb;
  }

  // After all new h-signatures are collected, check if there are TEBs sharing the same h-signature (relying to the same homotopy class)
  TebCandidateType::iterator cand_i = teb_candidates.begin();
  while (cand_i != teb_candidates.end())
  {
    TebCandidateType::iterator cand_j = std::find_if(teb_candidates.begin(),teb_candidates.end(), boost::bind(compareH,_1,cand_i->second));
    if (cand_j != teb_candidates.end() && cand_j != cand_i)
    {
      if ( cand_j->first->get()->getCurrentCost().sum() > cand_i->first->get()->getCurrentCost().sum() )
      {
	// found one that has higher cost, therefore erase cand_j
	tebs_.erase(cand_j->first);
	teb_candidates.erase(cand_j);
      }
      else   // otherwise erase cand_i
      {
	tebs_.erase(cand_i->first);
	cand_i = teb_candidates.erase(cand_i);
      }
    }
    else ++cand_i;	
  }
  /// now add the h-signatures to the internal lookup-table (but only if there is no existing duplicate)
  for (cand_i=teb_candidates.begin(); cand_i!=teb_candidates.end(); ++cand_i)
  {
    bool new_flag = addNewHSignatureIfNew(cand_i->second, cfg_->hcp.h_signature_threshold);
    if (!new_flag)
    {
      ROS_ERROR_STREAM("getAndFilterHomotopyClassesTEB() - This schould not be happen.");
      tebs_.erase(cand_i->first);
    }
  }
	
}
 
 
void HomotopyClassPlanner::exploreHomotopyClassesAndInitTebs(const PoseSE2& start, const PoseSE2& goal, double dist_to_obst, double hp_threshold)
{
  // first process old trajectories
  renewAndAnalyzeOldTebs(false);

  // now explore new homotopy classes and initialize tebs if new ones are found.
  if (cfg_->hcp.simple_exploration)
    createGraph(start,goal,dist_to_obst,cfg_->hcp.obstacle_heading_threshold!=0);
  else
    createProbRoadmapGraph(start,goal,dist_to_obst, cfg_->hcp.obstacle_heading_threshold!=0);
} 


void HomotopyClassPlanner::updateAllTEBs(boost::optional<const PoseSE2&> start, boost::optional<const PoseSE2&> goal,  boost::optional<const Eigen::Vector2d&> start_velocity)
{
  for (TebOptPlannerContainer::iterator it_teb = tebs_.begin(); it_teb != tebs_.end(); ++it_teb)
  {
    it_teb->get()->teb().updateAndPruneTEB(start, goal);
    if (start_velocity)
      it_teb->get()->setVelocityStart(*start_velocity);
  }
}

 
void HomotopyClassPlanner::optimizeAllTEBs(unsigned int iter_innerloop, unsigned int iter_outerloop)
{
  // optimize TEBs in parallel since they are independend of each other
  if (cfg_->hcp.enable_multithreading)
  {
    boost::thread_group teb_threads;
    for (TebOptPlannerContainer::iterator it_teb = tebs_.begin(); it_teb != tebs_.end(); ++it_teb)
    {
      teb_threads.create_thread( boost::bind(&TebOptimalPlanner::optimizeTEB, it_teb->get(), iter_innerloop, iter_outerloop, true) );
    }
    teb_threads.join_all();
  }
  else
  {
    for (TebOptPlannerContainer::iterator it_teb = tebs_.begin(); it_teb != tebs_.end(); ++it_teb)
    {
      it_teb->get()->optimizeTEB(iter_innerloop,iter_outerloop, true); // compute cost as well inside optimizeTEB (last argument = true)
    }
  }
} 
 
void HomotopyClassPlanner::deleteTebDetours(double threshold)
{
  TebOptPlannerContainer::iterator it_teb = tebs_.begin();
  while(it_teb != tebs_.end())
  {
    /// delete Detours if other TEBs will remain!
    if (tebs_.size()>1 && it_teb->get()->teb().detectDetoursBackwards(threshold)) 
      it_teb = tebs_.erase(it_teb); // 0.05
    else 
      ++it_teb;
  }
} 
 
 
TebOptimalPlannerPtr HomotopyClassPlanner::selectBestTeb()
{
  double min_cost = DBL_MAX; // maximum cost
  
  best_teb_.reset(); // reset current best_teb pointer

  for (TebOptPlannerContainer::iterator it_teb = tebs_.begin(); it_teb != tebs_.end(); ++it_teb)
  {
    double teb_cost = it_teb->get()->getCurrentCost().sum(); // just sum up all cost components

    if (teb_cost < min_cost)
    {
      best_teb_ = *it_teb;
      min_cost = teb_cost;
    }
  }	
  return best_teb_;
} 

bool HomotopyClassPlanner::isTrajectoryFeasible(base_local_planner::CostmapModel* costmap_model, const std::vector<geometry_msgs::Point>& footprint_spec,
						double inscribed_radius, double circumscribed_radius, int look_ahead_idx)
{
  if (!best_teb_)
    return false;
  
  if (look_ahead_idx < 0 || look_ahead_idx >= (int) best_teb_->teb().sizePoses())
    look_ahead_idx = (int) best_teb_->teb().sizePoses()-1;
  
  for (unsigned int i=0; i <= look_ahead_idx; ++i)
  {      
    if ( costmap_model->footprintCost(best_teb_->teb().Pose(i).x(), best_teb_->teb().Pose(i).y(), best_teb_->teb().Pose(i).theta(), footprint_spec, inscribed_radius, circumscribed_radius) < 0 )
      return false;
  }
  return true;
}

 
}