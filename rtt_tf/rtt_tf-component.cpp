/* (C) 2011 Ruben Smits, ruben.smits@mech.kuleuven.be, Department of Mechanical
 * Engineering, Katholieke Universiteit Leuven, Belgium.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "rtt_tf-component.hpp"
#include <rtt/Component.hpp>
#include <ros/ros.h>

#include <tf/tf.h>
#include <tf2/exceptions.h>

#include <geometry_msgs/TransformStamped.h>

#include <algorithm>

namespace rtt_tf
{
  // Functor for resolving the tf prefix when broadcasting multiple transforms.
  class PrefixResolver
  {
    public:
      PrefixResolver(const std::string& prefix) : prefix_(prefix) { }

      geometry_msgs::TransformStamped operator()(const geometry_msgs::TransformStamped& elem)
      {
        geometry_msgs::TransformStamped result = elem;
        result.header.frame_id = tf::resolve(prefix_, result.header.frame_id);
        result.child_frame_id = tf::resolve(prefix_, result.child_frame_id);
        return result;
      }

    private:
      const std::string& prefix_;
  };

  tf2_msgs::TFMessage transformsToMessage(const std::vector<geometry_msgs::TransformStamped>& tforms, const std::string& prefix)
  {
    tf2_msgs::TFMessage msg;
    // resolve names and copy transforms to message
    msg.transforms.reserve(tforms.size());
    std::transform(tforms.begin(), tforms.end(), msg.transforms.begin(), PrefixResolver(prefix));
    return msg;
  }

  using namespace RTT;

  RTT_TF::RTT_TF(const std::string& name) :
    TaskContext(name, PreOperational),
    tf2::BufferCore( ros::Duration(BufferCore::DEFAULT_CACHE_TIME) ),
    prop_cache_time( BufferCore::DEFAULT_CACHE_TIME ),
    prop_buffer_size(DEFAULT_BUFFER_SIZE)
  {
    this->addProperty("cache_time", prop_cache_time);
    this->addProperty("buffer_size", prop_buffer_size);
    this->addProperty("tf_prefix", prop_tf_prefix);
    this->addEventPort("tf_in", port_tf_in);
    this->addEventPort("tf_static_in", port_tf_static_in);
    this->addPort("tf_out", port_tf_out);

    this->addTFOperations(this->provides());
    this->addTFOperations(this->provides("tf"));
  }

  void RTT_TF::addTFOperations(RTT::Service::shared_ptr service)
  {
    service->addOperation("lookupTransform", &RTT_TF::lookupTransform, this)
      .doc("Lookup the most recent transform from source to target.")
      .arg("target", "target frame")
      .arg("source", "source frame");

    service->addOperation("lookupTransformAtTime", &RTT_TF::lookupTransformAtTime, this)
      .doc("Lookup the most recent transform from source to target at a specific time.")
      .arg("target", "Target frame")
      .arg("source", "Source frame")
      .arg("common_time", "[ros::Time] The common time at which the transform should be computed");

    service->addOperation("broadcastTransform", &RTT_TF::broadcastTransform, this, RTT::OwnThread)
      .doc("Broadcast a stamped transform immediately.")
      .arg("transform", "[geometry_msgs::TransformStamped]");

    service->addOperation("broadcastTransforms", &RTT_TF::broadcastTransforms, this, RTT::OwnThread)
      .doc("Broadcast stamped transforms immediately.")
      .arg("transforms", "[std::vector<geometry_msgs::TransformStamped>]");

    service->addOperation("broadcastStaticTransform", &RTT_TF::broadcastStaticTransform, this, RTT::OwnThread)
      .doc("Broadcast a stamped transform as a static transform immediately.")
      .arg("transform", "[geometry_msgs::TransformStamped]");

    service->addOperation("broadcastStaticTransforms", &RTT_TF::broadcastStaticTransforms, this, RTT::OwnThread)
      .doc("Broadcast stamped transforms as static transforms immediately.")
      .arg("transforms", "[std::vector<geometry_msgs::TransformStamped>]");

    service->addOperation("canTransform", &RTT_TF::canTransform, this)
      .doc("Check if the transform from source to target can be resolved.")
      .arg("target", "Target frame")
      .arg("source", "Source frame");

    service->addOperation("canTransformAtTime", &RTT_TF::canTransformAtTime, this)
      .doc("Check if the transform from source to target can be resolved for a given common time.")
      .arg("target", "Target frame")
      .arg("source", "Source frame")
      .arg("common_time", "[ros::Time] The common time for which the transform would resolve");
  }

  bool RTT_TF::configureHook()
  {
    Logger::In(this->getName());

    // Get tf prefix rosparam
    ros::NodeHandle nh("~");
    std::string tf_prefix_param_key;
    if(nh.searchParam("tf_prefix",tf_prefix_param_key)) {
      nh.getParam(tf_prefix_param_key, prop_tf_prefix);
    }
    
    // Connect to tf topic
    ConnPolicy cp = ConnPolicy::buffer(prop_buffer_size);
    cp.transport = 3; //3=ROS
    cp.name_id = "/tf";

    // Connect to tf_static topic
    ConnPolicy cp_static = ConnPolicy::buffer(prop_buffer_size);
    cp_static.transport = 3; //3=ROS
    cp_static.name_id = "/tf_static";

    bool configured = port_tf_static_in.createStream(cp_static)
                    && port_tf_in.createStream(cp)
                    && port_tf_out.createStream(cp)
                    && port_tf_static_out.createStream(cp_static);

    if (!configured) {
      cleanupHook();
    }

    return configured;
  }

  void RTT_TF::internalUpdate(tf2_msgs::TFMessage& msg, RTT::InputPort<tf2_msgs::TFMessage>& port, bool is_static)
  {
    // tf2::BufferCore::setTransform (see #68) has a non-defaulted authority argument,
    //  but there is no __connection_header to extract it from.
    const std::string authority = "unknown_authority";

    while (port.read(msg) == NewData) {
      for (std::size_t i = 0; i < msg.transforms.size(); ++i) {
        try {
          this->setTransform(msg.transforms[i], authority, is_static);
        } catch (tf2::TransformException& ex) {
          log(Error) << "Failure to set received transform from "
            << msg.transforms[i].child_frame_id << " to "
            << msg.transforms[i].header.frame_id
            << " with error: " << ex.what() << endlog();
        }
      }
    }
  }

  void RTT_TF::updateHook()
  {
    Logger::In(this->getName());
#ifndef NDEBUG
    //log(Debug) << "In update" << endlog();
#endif
    try
    {
      tf2_msgs::TFMessage msg_in;
      internalUpdate(msg_in, port_tf_in, false);
      internalUpdate(msg_in, port_tf_static_in, true);
    }
    catch (std::exception& ex)
    {
      log(Error) << ex.what() << endlog();
    }
  }

  void RTT_TF::cleanupHook()
  {
    port_tf_in.disconnect();
    port_tf_out.disconnect();
    port_tf_static_in.disconnect();
    port_tf_static_out.disconnect();
  }

  ros::Time RTT_TF::getLatestCommonTime(
      const std::string& target,
      const std::string& source) const
  {
    ros::Time common_time;

    tf2::CompactFrameID target_id = _lookupFrameNumber(target);
    tf2::CompactFrameID source_id = _lookupFrameNumber(source);

    _getLatestCommonTime(source_id, target_id, common_time, NULL);

    return common_time;
  }

  geometry_msgs::TransformStamped RTT_TF::lookupTransform(
      const std::string& target,
      const std::string& source) const
  {
    return tf2::BufferCore::lookupTransform(target, source, ros::Time());
  }

  bool RTT_TF::canTransform(
      const std::string& target,
      const std::string& source) const
  {
    return tf2::BufferCore::canTransform(target, source, ros::Time());
  }

  bool RTT_TF::canTransformAtTime(
      const std::string& target,
      const std::string& source,
      const ros::Time& common_time) const
  {
    return tf2::BufferCore::canTransform(target, source, common_time);
  }

  geometry_msgs::TransformStamped RTT_TF::lookupTransformAtTime(
      const std::string& target,
      const std::string& source,
      const ros::Time& common_time) const
  {
    return tf2::BufferCore::lookupTransform(target, source, common_time);
  }

  void RTT_TF::broadcastTransform(const geometry_msgs::TransformStamped& tform)
  {
    const std::vector<geometry_msgs::TransformStamped> tforms(1, tform);
    tf2_msgs::TFMessage msg_out = transformsToMessage(tforms, prop_tf_prefix);
    port_tf_out.write(msg_out);
  }

  void RTT_TF::broadcastTransforms(const std::vector<geometry_msgs::TransformStamped>& tform)
  {
    tf2_msgs::TFMessage msg_out = transformsToMessage(tform, prop_tf_prefix);
    port_tf_out.write(msg_out);
  }

  void RTT_TF::broadcastStaticTransform(const geometry_msgs::TransformStamped& tform)
  {
    const std::vector<geometry_msgs::TransformStamped> tforms(1, tform);
    tf2_msgs::TFMessage msg_out = transformsToMessage(tforms, prop_tf_prefix);
    port_tf_static_out.write(msg_out);
  }

  void RTT_TF::broadcastStaticTransforms(const std::vector<geometry_msgs::TransformStamped>& tform)
  {
    tf2_msgs::TFMessage msg_out = transformsToMessage(tform, prop_tf_prefix);
    port_tf_static_out.write(msg_out);
  }

}//namespace

/*
 * Using this macro, only one component may live
 * in one library *and* you may *not* link this library
 * with another component library. Use 
 * ORO_CREATE_COMPONENT_TYPE()
 * ORO_LIST_COMPONENT_TYPE(Rtt_tf)
 * In case you want to link with another library that
 * already contains components.
 *
 * If you have put your component class
 * in a namespace, don't forget to add it here too:
 */
ORO_CREATE_COMPONENT(rtt_tf::RTT_TF)
