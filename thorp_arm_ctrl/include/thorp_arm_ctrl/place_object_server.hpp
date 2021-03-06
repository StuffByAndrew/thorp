/*
 * Author: Jorge Santos
 */

#pragma once


// Thorp pickup object action server
#include <actionlib/server/simple_action_server.h>
#include <thorp_msgs/PlaceObjectAction.h>

// MoveIt! pickup action client
#include <actionlib/client/simple_action_client.h>
#include <moveit_msgs/PlaceAction.h>

#include "thorp_arm_ctrl/thorp_arm_controller.hpp"


namespace thorp_arm_ctrl
{

class PlaceObjectServer : public ThorpArmController
{
private:
  // Thorp pickup object action server
  actionlib::SimpleActionServer<thorp_msgs::PlaceObjectAction> as_;
  std::string action_name_;

  // MoveIt! place action client
  actionlib::SimpleActionClient<moveit_msgs::PlaceAction> ac_;

  const int PLACE_ATTEMPTS = 5;

public:
  PlaceObjectServer(const std::string name);
  ~PlaceObjectServer();

  void executeCB(const thorp_msgs::PlaceObjectGoal::ConstPtr& goal);
  void preemptCB();

private:
  int32_t place(const std::string& obj_name, const std::string& surface, const geometry_msgs::PoseStamped& pose);

  int32_t makePlaceLocations(const geometry_msgs::PoseStamped& target_pose,
                             const geometry_msgs::Pose& attached_obj_pose,
                             const std::string& obj_name, const std::string& surface,
                             std::vector<moveit_msgs::PlaceLocation>& place_locations);
};

};
