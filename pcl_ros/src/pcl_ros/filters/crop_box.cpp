/*
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2010, Willow Garage, Inc.
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
 *   * Neither the name of Willow Garage, Inc. nor the names of its
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
 * $Id: cropbox.cpp
 *
 */

#include "pcl_ros/filters/crop_box.hpp"

pcl_ros::CropBox::CropBox(const rclcpp::NodeOptions & options)
: Filter("CropBoxNode", options)
{
  use_frame_params();
  std::vector<std::string> param_names = add_common_params();

  callback_handle_ =
    add_on_set_parameters_callback(
    std::bind(
      &CropBox::config_callback, this,
      std::placeholders::_1));

  config_callback(get_parameters(param_names));
  // TODO(daisukes): lazy subscription after rclcpp#2060
  subscribe();
}

//////////////////////////////////////////////////////////////////////////////////////////////

rcl_interfaces::msg::SetParametersResult
pcl_ros::CropBox::config_callback(const std::vector<rclcpp::Parameter> & params)
{
  std::lock_guard<std::mutex> lock(mutex_);

  Eigen::Vector4f min_point, max_point;
  min_point = impl_.getMin();
  max_point = impl_.getMax();

  for (const rclcpp::Parameter & param : params) {
    if (param.get_name() == "min_x") {
      min_point(0) = param.as_double();
    }
    if (param.get_name() == "max_x") {
      max_point(0) = param.as_double();
    }
    if (param.get_name() == "min_y") {
      min_point(1) = param.as_double();
    }
    if (param.get_name() == "max_y") {
      max_point(1) = param.as_double();
    }
    if (param.get_name() == "min_z") {
      min_point(2) = param.as_double();
    }
    if (param.get_name() == "max_z") {
      max_point(2) = param.as_double();
    }
    if (param.get_name() == "filter_limit_negative") {
      // Check the current value for the negative flag
      if (impl_.getNegative() != param.as_bool()) {
        RCLCPP_DEBUG(
          get_logger(), "Setting the filter negative flag to: %s.",
          param.as_bool() ? "true" : "false");
        // Call the virtual method in the child
        impl_.setNegative(param.as_bool());
      }
    }
    if (param.get_name() == "keep_organized") {
      // Check the current value for keep_organized
      if (impl_.getKeepOrganized() != param.as_bool()) {
        RCLCPP_DEBUG(
          get_logger(), "Setting the filter keep_organized value to: %s.",
          param.as_bool() ? "true" : "false");
        // Call the virtual method in the child
        impl_.setKeepOrganized(param.as_bool());
      }
    }
  }

  // Check the current values for minimum point
  if (min_point != impl_.getMin()) {
    RCLCPP_DEBUG(get_logger(), "Setting the minimum point to: %f %f %f.",
      min_point(0), min_point(1), min_point(2));
    impl_.setMin(min_point);
  }
  
  // Check the current values for the maximum point
  if (max_point != impl_.getMax()) {
    RCLCPP_DEBUG(get_logger(), "Setting the maximum point to: %f %f %f.",
      max_point(0), max_point(1), max_point(2));
    impl_.setMax(max_point);
  }

  // TODO(sloretz) constraint validation
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  return result;
}

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(pcl_ros::CropBox)
