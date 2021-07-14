#pragma once
#ifndef SUPPORT_H
#define SUPPORT_H

/* includes //{ */
#include <ros/publisher.h>

#include <geometry_msgs/Quaternion.h>

#include <mrs_lib/attitude_converter.h>

#include <nav_msgs/Odometry.h>

//}

namespace mrs_uav_odometry
{

tf2::Transform tf2FromPose(const geometry_msgs::Pose& pose_in);

geometry_msgs::Pose poseFromTf2(const tf2::Transform& tf_in);

geometry_msgs::Vector3 pointToVector3(const geometry_msgs::Point& point_in);

double distance(const nav_msgs::Odometry& odom1, const nav_msgs::Odometry& odom2);

std::string toLowercase(const std::string str_in);

std::string toUppercase(const std::string str_in);

template <typename MsgType>
void tryPublish(const ros::Publisher& pub, const MsgType& msg);

bool noNans(const geometry_msgs::TransformStamped& tf);

bool noNans(const geometry_msgs::Quaternion& q);

bool isZeroQuaternion(const geometry_msgs::Quaternion& q);

}  // namespace mrs_uav_odometry

#endif
