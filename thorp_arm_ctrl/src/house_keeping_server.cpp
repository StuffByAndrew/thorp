/*
 * Author: Jorge Santos
 */

#include <moveit_msgs/GetPlanningScene.h>

#include "thorp_arm_ctrl/house_keeping_server.hpp"


namespace thorp_arm_ctrl
{


HouseKeepingServer::HouseKeepingServer()
{
  ros::NodeHandle nh;

  arm_trajectory_pub_ = nh.advertise<trajectory_msgs::JointTrajectory>("arm_controller/command", 1);
  force_resting_srv_  = nh.advertiseService("force_resting", &HouseKeepingServer::forceRestingCB, this);
  clear_gripper_srv_  = nh.advertiseService("clear_gripper", &HouseKeepingServer::clearGripperCB, this);
  gripper_busy_srv_   = nh.advertiseService("gripper_busy",  &HouseKeepingServer::gripperBusyCB,  this);

  // Get default planning scene so I can restore it after temporal changes
  planning_scene_srv_ = nh.serviceClient<moveit_msgs::GetPlanningScene>("get_planning_scene");
  if (planning_scene_srv_.waitForExistence(ros::Duration(30.0)))
  {
    moveit_msgs::GetPlanningScene srv;
    srv.request.components.components |= moveit_msgs::PlanningSceneComponents::ALLOWED_COLLISION_MATRIX;
    if (planning_scene_srv_.call(srv))
    {
      planning_scene_ = srv.response.scene;
    }
    else
    {
      ROS_ERROR("[house keeping] Failed to call service get_planning_scene");
    }
  }
  else
  {
    ROS_ERROR("[house keeping] Service get_planning_scene not available after 30s");
  }
}

HouseKeepingServer::~HouseKeepingServer()
{
}


bool HouseKeepingServer::forceRestingCB(std_srvs::EmptyRequest &request, std_srvs::EmptyResponse &response)
{
  if (planning_scene_.allowed_collision_matrix.entry_values.empty())
  {
    ROS_ERROR("[house keeping] Not master planning_scene stored, so cannot alter it");
    return false;
  }

  ROS_INFO("[house keeping] Executing force resting service");

  moveit_msgs::PlanningScene ps = planning_scene_;
  ps.is_diff = true;
  for (auto& entry_values : ps.allowed_collision_matrix.entry_values)
    for (auto& value : entry_values.enabled)
      value = true;

  bool moved = false;
  if (planning_scene_interface().applyPlanningScene(ps))
  {
    if (arm().setNamedTarget("resting"))
    {
      moveit::planning_interface::MoveItErrorCode result = arm().move();
      if (result)
      {
        ROS_INFO("[house keeping] Move to target 'resting' completed");
        moved = true;
      }
      else
      {
        ROS_ERROR("[house keeping] Move to target 'resting' failed: %s", mec2str(result));
      }
    }
    else
    {
      ROS_ERROR("[house keeping] Set named target 'resting' failed");
    }
  }
  else
  {
    ROS_ERROR("[house keeping] Apply relaxed planning scene failed");
  }

  if (!planning_scene_interface().applyPlanningScene(planning_scene_))
  {
    ROS_ERROR("[house keeping] Restore original planning scene failed");
  }

  return moved;


// PREVIOUS ATTEMPTS
///  planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor_ptr;
//  planning_scene_monitor::LockedPlanningSceneRW lps(planning_scene_monitor_ptr_);
//  collision_detection::AllowedCollisionMatrix acm = lps->getAllowedCollisionMatrix();
//  lps->getAllowedCollisionMatrixNonConst().setEntry(true);
//  lps->getAllowedCollisionMatrixNonConst().setEntry("arm_mount_link", "arm_elbow_flex_servo_link", true);
//
//  acm.print(std::cout);
//  ROS_ERROR_STREAM(""<< acm.getSize());
//  ROS_ERROR_STREAM(""<< acm.getSize());
//  ROS_ERROR_STREAM(""<< acm.getSize());
//  ROS_ERROR_STREAM(""<< acm.getSize());
//  lps->getAllowedCollisionMatrixNonConst().print(std::cout);

//  this kinda works
  std::map<std::string, double> joints = arm().getNamedTargetValues("resting");
  if (joints.size() < 4)
  {
    ROS_ERROR("[house keeping] 'resting' named target only contains %lu joints (at least 4 required)", joints.size());
    return false;
  }

  // send a goal to the action
  trajectory_msgs::JointTrajectory traj;
  traj.header.stamp = ros::Time::now();
  traj.points.resize(1);
  traj.points[0].time_from_start = ros::Duration(5.0);
  for (auto joint : joints)
  {
    if (joint.first.find("arm_") == 0)
    {
      ROS_ERROR_STREAM(""<< joint.first<<"   "<< joint.second);
      traj.joint_names.push_back(joint.first);
      traj.points[0].positions.push_back(joint.second);
    }
  }

  arm_trajectory_pub_.publish(traj);
  return true;
}

bool HouseKeepingServer::clearGripperCB(std_srvs::EmptyRequest &request, std_srvs::EmptyResponse &response)
{
  ROS_INFO("[house keeping] Ensure we don't retain any object attached to the gripper");

  bool detach_result = arm().detachObject();
  // Remove the detached object from the planning scene so the gripper doesn't "collide" with it
  planning_scene_interface().removeCollisionObjects(std::vector<std::string>{attached_object});
  attached_object = "";
  bool gripper_result = setGripper(gripper_open, false);
  return gripper_result && detach_result;
}

bool HouseKeepingServer::gripperBusyCB(std_srvs::TriggerRequest &request, std_srvs::TriggerResponse &response)
{
  std::map<std::string, moveit_msgs::AttachedCollisionObject> attached_objects =
    planning_scene_interface().getAttachedObjects();
  for (const auto& ao: attached_objects)
    ROS_ERROR_STREAM(" " << ao.first<< "     "<< ao.second.object.id<<  "    mio: " <<attached_object);

  if (!attached_objects.empty())
  {
    ROS_ASSERT_MSG(attached_objects.size()==1, "More than one (%lu) attached objects???", attached_objects.size());
    response.success = true;
    response.message = attached_objects.begin()->first;
  }
  else
    response.success = false;

  return true;
  // TODO:  this is much simpler and reliable... I keep by now, until I can do a "physical check", still interesting, but too hard


  // Check if we are holding an object by closing a bit the gripper and measuring if joint effort increases
  double opening_before = joint_state_watchdog_.gripperOpening();
  double effort_before = joint_state_watchdog_.gripperEffort();

  bool gripper_result = setGripper(opening_before - 0.001, true);
  double effort_after = joint_state_watchdog_.gripperEffort();

  bool gripper_busy = (effort_before - effort_after) > 0.01;
  response.success = gripper_busy;
  response.message = attached_object;

  // if (!gripper_busy && !attached_object.empty())   someone should call clearGripperCB if this is true, but i let for the executive
  //  bool detach_result = arm().detachObject();
  // Remove the detached object from the planning scene so the gripper doesn't "collide" with it
  //planning_scene_interface().removeCollisionObjects(std::vector<std::string>{attached_object});
  //attached_object = "";

  if (gripper_busy)
    ROS_INFO("[house keeping] Gripper is busy %s",
             attached_object.empty() ? "but no object is attached" : ("with object " + attached_object).c_str());
  else
    ROS_INFO("[house keeping] Gripper is free %s",
             !attached_object.empty() ? ("but we have an object attached: " + attached_object).c_str() : "");

  // restore gripper original opening
  setGripper(opening_before, false);

  return gripper_result;
}

};
