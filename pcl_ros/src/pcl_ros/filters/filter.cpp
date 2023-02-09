/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
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
 * $Id: filter.cpp 35876 2011-02-09 01:04:36Z rusu $
 *
 */

#include "pcl_ros/filters/filter.hpp"
#include <pcl/common/io.h>
#include "pcl_ros/transforms.hpp"

/*//#include <pcl/filters/pixel_grid.h>
//#include <pcl/filters/filter_dimension.h>
*/

/*//typedef pcl::PixelGrid PixelGrid;
//typedef pcl::FilterDimension FilterDimension;
*/

// Include the implementations instead of compiling them separately to speed up compile time
// #include "extract_indices.cpp"
// #include "passthrough.cpp"
// #include "project_inliers.cpp"
// #include "radius_outlier_removal.cpp"
// #include "statistical_outlier_removal.cpp"
// #include "voxel_grid.cpp"

/*//PLUGINLIB_EXPORT_CLASS(PixelGrid,nodelet::Nodelet);
//PLUGINLIB_EXPORT_CLASS(FilterDimension,nodelet::Nodelet);
*/

///////////////////////////////////////////////////////////////////////////////////////////////////
void
pcl_ros::Filter::computePublish(
  const PointCloud2::ConstSharedPtr & input,
  const IndicesPtr & indices)
{
  PointCloud2 output;
  // Call the virtual method in the child
  filter(input, indices, output);

  PointCloud2::UniquePtr cloud_tf(new PointCloud2(output));     // set the output by default
  // Check whether the user has given a different output TF frame
  if (!tf_output_frame_.empty() && output.header.frame_id != tf_output_frame_) {
    RCLCPP_DEBUG(
      this->get_logger(), "Transforming output dataset from %s to %s.",
      output.header.frame_id.c_str(), tf_output_frame_.c_str());
    // Convert the cloud into the different frame
    PointCloud2 cloud_transformed;
    if (!pcl_ros::transformPointCloud(tf_output_frame_, output, cloud_transformed, tf_buffer_)) {
      RCLCPP_ERROR(
        this->get_logger(), "Error converting output dataset from %s to %s.",
        output.header.frame_id.c_str(), tf_output_frame_.c_str());
      return;
    }
    cloud_tf.reset(new PointCloud2(cloud_transformed));
  }
  if (tf_output_frame_.empty() && output.header.frame_id != tf_input_orig_frame_) {
    // no tf_output_frame given, transform the dataset to its original frame
    RCLCPP_DEBUG(
      this->get_logger(), "Transforming output dataset from %s back to %s.",
      output.header.frame_id.c_str(), tf_input_orig_frame_.c_str());
    // Convert the cloud into the different frame
    PointCloud2 cloud_transformed;
    if (!pcl_ros::transformPointCloud(
        tf_input_orig_frame_, output, cloud_transformed,
        tf_buffer_))
    {
      RCLCPP_ERROR(
        this->get_logger(), "Error converting output dataset from %s back to %s.",
        output.header.frame_id.c_str(), tf_input_orig_frame_.c_str());
      return;
    }
    cloud_tf.reset(new PointCloud2(cloud_transformed));
  }

  // Copy timestamp to keep it
  cloud_tf->header.stamp = input->header.stamp;

  // Publish the unique ptr
  pub_output_->publish(move(cloud_tf));
}

//////////////////////////////////////////////////////////////////////////////////////////////
void
pcl_ros::Filter::subscribe()
{
  // If we're supposed to look for PointIndices (indices)
  if (use_indices_) {
    // Subscribe to the input using a filter
    auto sensor_qos_profile = rclcpp::QoS(
      rclcpp::KeepLast(max_queue_size_),
      rmw_qos_profile_sensor_data).get_rmw_qos_profile();
    sub_input_filter_.subscribe(this, "input", sensor_qos_profile);
    sub_indices_filter_.subscribe(this, "indices", sensor_qos_profile);

    if (approximate_sync_) {
      sync_input_indices_a_ =
        std::make_shared<message_filters::Synchronizer<sync_policies::ApproximateTime<PointCloud2,
          pcl_msgs::msg::PointIndices>>>(max_queue_size_);
      sync_input_indices_a_->connectInput(sub_input_filter_, sub_indices_filter_);
      sync_input_indices_a_->registerCallback(
        std::bind(
          &Filter::input_indices_callback, this,
          std::placeholders::_1, std::placeholders::_2));
    } else {
      sync_input_indices_e_ =
        std::make_shared<message_filters::Synchronizer<sync_policies::ExactTime<PointCloud2,
          pcl_msgs::msg::PointIndices>>>(max_queue_size_);
      sync_input_indices_e_->connectInput(sub_input_filter_, sub_indices_filter_);
      sync_input_indices_e_->registerCallback(
        std::bind(
          &Filter::input_indices_callback, this,
          std::placeholders::_1, std::placeholders::_2));
    }
  } else {
    // Workaround for a callback with custom arguments ros2/rclcpp#766
    std::function<void(PointCloud2::ConstSharedPtr)> callback =
      std::bind(&Filter::input_indices_callback, this, std::placeholders::_1, nullptr);

    // Subscribe in an old fashion to input only (no filters)
    sub_input_ =
      this->create_subscription<PointCloud2>(
      "input", max_queue_size_,
      callback);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////
void
pcl_ros::Filter::unsubscribe()
{
  if (use_indices_) {
    sub_input_filter_.unsubscribe();
    sub_indices_filter_.unsubscribe();
  } else {
    sub_input_.reset();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////
pcl_ros::Filter::Filter(std::string node_name, const rclcpp::NodeOptions & options)
: PCLNode(node_name, options)
{
  pub_output_ = create_publisher<PointCloud2>("output", max_queue_size_);
  RCLCPP_DEBUG(this->get_logger(), "Node successfully created.");
}

//////////////////////////////////////////////////////////////////////////////////////////////
void
pcl_ros::Filter::use_frame_params()
{
  rcl_interfaces::msg::ParameterDescriptor input_frame_desc;
  input_frame_desc.name = "input_frame";
  input_frame_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_STRING;
  input_frame_desc.description =
    "The input TF frame the data should be transformed into before processing, "
    "if input.header.frame_id is different.";
  declare_parameter(input_frame_desc.name, rclcpp::ParameterValue(""), input_frame_desc);

  rcl_interfaces::msg::ParameterDescriptor output_frame_desc;
  output_frame_desc.name = "output_frame";
  output_frame_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_STRING;
  output_frame_desc.description =
    "The output TF frame the data should be transformed into after processing, "
    "if input.header.frame_id is different.";
  declare_parameter(output_frame_desc.name, rclcpp::ParameterValue(""), output_frame_desc);

  // Validate initial values using same callback
  callback_handle_ =
    add_on_set_parameters_callback(
    std::bind(
      &Filter::config_callback, this,
      std::placeholders::_1));

  std::vector<std::string> param_names{input_frame_desc.name, output_frame_desc.name};
  auto result = config_callback(get_parameters(param_names));
  if (!result.successful) {
    throw std::runtime_error(result.reason);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////
std::vector<std::string>
pcl_ros::Filter::add_common_params()
{
  // filter: Passthrough

  rcl_interfaces::msg::ParameterDescriptor ffn_desc;
  ffn_desc.name = "filter_field_name";
  ffn_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_STRING;
  ffn_desc.description = "The field name used for filtering";
  declare_parameter(ffn_desc.name, rclcpp::ParameterValue("z"), ffn_desc);

  rcl_interfaces::msg::ParameterDescriptor flmin_desc;
  flmin_desc.name = "filter_limit_min";
  flmin_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  flmin_desc.description = "The minimum allowed field value a point will be considered from";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -100000.0;
    float_range.to_value = 100000.0;
    flmin_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(flmin_desc.name, rclcpp::ParameterValue(0.0), flmin_desc);

  rcl_interfaces::msg::ParameterDescriptor flmax_desc;
  flmax_desc.name = "filter_limit_max";
  flmax_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  flmax_desc.description = "The maximum allowed field value a point will be considered from";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -100000.0;
    float_range.to_value = 100000.0;
    flmax_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(flmax_desc.name, rclcpp::ParameterValue(1.0), flmax_desc);

  rcl_interfaces::msg::ParameterDescriptor flneg_desc;
  flneg_desc.name = "filter_limit_negative";
  flneg_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL;
  flneg_desc.description =
    "Set to true if we want to return the data outside [filter_limit_min; filter_limit_max].";
  declare_parameter(flneg_desc.name, rclcpp::ParameterValue(false), flneg_desc);

  rcl_interfaces::msg::ParameterDescriptor keep_organized_desc;
  keep_organized_desc.name = "keep_organized";
  keep_organized_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL;
  keep_organized_desc.description =
    "Set whether the filtered points should be kept and set to NaN, "
    "or removed from the PointCloud, thus potentially breaking its organized structure.";
  declare_parameter(keep_organized_desc.name, rclcpp::ParameterValue(false), keep_organized_desc);

  // filter: VoxelGrid

  rcl_interfaces::msg::ParameterDescriptor leaf_size_desc;
  leaf_size_desc.name = "leaf_size";
  leaf_size_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  leaf_size_desc.description =
    "The size of a leaf (on x,y,z) used for downsampling";
  declare_parameter(leaf_size_desc.name, rclcpp::ParameterValue(0.01), leaf_size_desc);

  // filter: CropBox

  rcl_interfaces::msg::ParameterDescriptor min_x_desc;
  min_x_desc.name = "min_x";
  min_x_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  min_x_desc.description =
    "Minimum x value below which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    min_x_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(min_x_desc.name, rclcpp::ParameterValue(-1.0), min_x_desc);
  
  rcl_interfaces::msg::ParameterDescriptor max_x_desc;
  max_x_desc.name = "max_x";
  max_x_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  max_x_desc.description =
    "Maximum x value above which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    max_x_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(max_x_desc.name, rclcpp::ParameterValue(1.0), max_x_desc);

  rcl_interfaces::msg::ParameterDescriptor min_y_desc;
  min_y_desc.name = "min_y";
  min_y_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  min_y_desc.description =
    "Minimum y value below which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    min_y_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(min_y_desc.name, rclcpp::ParameterValue(-1.0), min_y_desc);
  
  rcl_interfaces::msg::ParameterDescriptor max_y_desc;
  max_y_desc.name = "max_y";
  max_y_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  max_y_desc.description =
    "Maximum y value above which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    max_y_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(max_y_desc.name, rclcpp::ParameterValue(1.0), max_y_desc);

  rcl_interfaces::msg::ParameterDescriptor min_z_desc;
  min_z_desc.name = "min_z";
  min_z_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  min_z_desc.description =
    "Minimum z value below which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    min_z_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(min_z_desc.name, rclcpp::ParameterValue(-1.0), min_z_desc);
  
  rcl_interfaces::msg::ParameterDescriptor max_z_desc;
  max_z_desc.name = "max_z";
  max_z_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  max_z_desc.description =
    "Maximum z value above which points will be removed";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = -1000.0;
    float_range.to_value = 1000.0;
    max_z_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(max_z_desc.name, rclcpp::ParameterValue(1.0), max_z_desc);

  // filter: RadiusOutlierRemoval

  rcl_interfaces::msg::ParameterDescriptor min_neighbors_desc;
  min_neighbors_desc.name = "min_neighbors";
  min_neighbors_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER;
  min_neighbors_desc.description =
    "The number of neighbors that need to be present in order to be classified as an inlier.";
  {
    rcl_interfaces::msg::IntegerRange int_range;
    int_range.from_value = 0;
    int_range.to_value = 1000;
    min_neighbors_desc.integer_range.push_back(int_range);
  }
  declare_parameter(min_neighbors_desc.name, rclcpp::ParameterValue(5), min_neighbors_desc);

  rcl_interfaces::msg::ParameterDescriptor radius_search_desc;
  radius_search_desc.name = "radius_search";
  radius_search_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  radius_search_desc.description =
    "Radius of the sphere that will determine which points are neighbors.";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = 0.0;
    float_range.to_value = 10.0;
    radius_search_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(radius_search_desc.name, rclcpp::ParameterValue(0.1), radius_search_desc);

  // filter: StatisticalOutlierRemoval

  rcl_interfaces::msg::ParameterDescriptor mean_k_desc;
  mean_k_desc.name = "mean_k";
  mean_k_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER;
  mean_k_desc.description =
    "The number of points (k) to use for mean distance estimation.";
  {
    rcl_interfaces::msg::IntegerRange int_range;
    int_range.from_value = 2;
    int_range.to_value = 100;
    mean_k_desc.integer_range.push_back(int_range);
  }
  declare_parameter(mean_k_desc.name, rclcpp::ParameterValue(2), mean_k_desc);

  rcl_interfaces::msg::ParameterDescriptor stddev_desc;
  stddev_desc.name = "stddev";
  stddev_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  stddev_desc.description =
    "The standard deviation multiplier threshold. \
    All points outside the mean +- sigma * std_mul will be considered outliers.";
  {
    rcl_interfaces::msg::FloatingPointRange float_range;
    float_range.from_value = 0.0;
    float_range.to_value = 5.0;
    stddev_desc.floating_point_range.push_back(float_range);
  }
  declare_parameter(stddev_desc.name, rclcpp::ParameterValue(0.0), stddev_desc);

  rcl_interfaces::msg::ParameterDescriptor negative_desc;
  negative_desc.name = "negative";
  negative_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL;
  negative_desc.description =
    "Set whether the inliers should be returned (true) or the outliers (false).";
  declare_parameter(negative_desc.name, rclcpp::ParameterValue(false), negative_desc);

  return std::vector<std::string> {
    ffn_desc.name,
    flmin_desc.name,
    flmax_desc.name,
    flneg_desc.name,
    keep_organized_desc.name,
    leaf_size_desc.name,
    min_x_desc.name,
    max_x_desc.name,
    min_y_desc.name,
    max_y_desc.name,
    min_z_desc.name,
    max_z_desc.name,
    min_neighbors_desc.name,
    radius_search_desc.name,
    mean_k_desc.name,
    stddev_desc.name,
    negative_desc.name,
  };
}

//////////////////////////////////////////////////////////////////////////////////////////////
rcl_interfaces::msg::SetParametersResult
pcl_ros::Filter::config_callback(const std::vector<rclcpp::Parameter> & params)
{
  std::lock_guard<std::mutex> lock(mutex_);

  for (const rclcpp::Parameter & param : params) {
    if (param.get_name() == "input_frame") {
      if (tf_input_frame_ != param.as_string()) {
        tf_input_frame_ = param.as_string();
        RCLCPP_DEBUG(get_logger(), "Setting the input frame to: %s.", tf_input_frame_.c_str());
      }
    }
    if (param.get_name() == "output_frame") {
      if (tf_output_frame_ != param.as_string()) {
        tf_output_frame_ = param.as_string();
        RCLCPP_DEBUG(get_logger(), "Setting the output frame to: %s.", tf_output_frame_.c_str());
      }
    }
  }
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  return result;
}

//////////////////////////////////////////////////////////////////////////////////////////////
void
pcl_ros::Filter::input_indices_callback(
  const PointCloud2::ConstSharedPtr & cloud,
  const PointIndices::ConstSharedPtr & indices)
{
  // If cloud is given, check if it's valid
  if (!isValid(cloud)) {
    RCLCPP_ERROR(this->get_logger(), "Invalid input!");
    return;
  }
  // If indices are given, check if they are valid
  if (indices && !isValid(indices)) {
    RCLCPP_ERROR(this->get_logger(), "Invalid indices!");
    return;
  }

  /// DEBUG
  if (indices) {
    RCLCPP_DEBUG(
      this->get_logger(), "[input_indices_callback]\n"
      "  - PointCloud with %d data points (%s), stamp %d.%09d, and frame %s on topic %s received.\n"
      "  - PointIndices with %zu values, stamp %d.%09d, and frame %s on topic %s received.",
      cloud->width * cloud->height, pcl::getFieldsList(*cloud).c_str(),
      cloud->header.stamp.sec, cloud->header.stamp.nanosec, cloud->header.frame_id.c_str(), "input",
      indices->indices.size(), indices->header.stamp.sec, indices->header.stamp.nanosec,
      indices->header.frame_id.c_str(), "indices");
  } else {
    RCLCPP_DEBUG(
      this->get_logger(), "PointCloud with %d data points and frame %s on topic %s received.",
      cloud->width * cloud->height, cloud->header.frame_id.c_str(), "input");
  }
  ///

  // Check whether the user has given a different input TF frame
  tf_input_orig_frame_ = cloud->header.frame_id;
  PointCloud2::ConstSharedPtr cloud_tf;
  if (!tf_input_frame_.empty() && cloud->header.frame_id != tf_input_frame_) {
    RCLCPP_DEBUG(
      this->get_logger(), "Transforming input dataset from %s to %s.",
      cloud->header.frame_id.c_str(), tf_input_frame_.c_str());
    // Save the original frame ID
    // Convert the cloud into the different frame
    PointCloud2 cloud_transformed;
    if (!pcl_ros::transformPointCloud(tf_input_frame_, *cloud, cloud_transformed, tf_buffer_)) {
      RCLCPP_ERROR(
        this->get_logger(), "Error converting input dataset from %s to %s.",
        cloud->header.frame_id.c_str(), tf_input_frame_.c_str());
      return;
    }
    cloud_tf = std::make_shared<PointCloud2>(cloud_transformed);
  } else {
    cloud_tf = cloud;
  }

  // Need setInputCloud () here because we have to extract x/y/z
  IndicesPtr vindices;
  if (indices) {
    vindices.reset(new std::vector<int>(indices->indices));
  }

  computePublish(cloud_tf, vindices);
}
