/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/localization/msf/local_map/base_map/base_map_pool.h"

#include "cyber/common/log.h"
#include "modules/localization/msf/local_map/base_map/base_map_config.h"
#include "modules/localization/msf/local_map/base_map/base_map_node.h"
#include "modules/localization/msf/local_map/base_map/base_map_node_index.h"

namespace apollo {
namespace localization {
namespace msf {

BaseMapNodePool::BaseMapNodePool(unsigned int pool_size,
                                 unsigned int thread_size)
    : pool_size_(pool_size) {}

BaseMapNodePool::~BaseMapNodePool() { Release(); }

void BaseMapNodePool::Initial(const BaseMapConfig* map_config,
                              bool is_fixed_size) {
  is_fixed_size_ = is_fixed_size;
  map_config_ = map_config;
  for (unsigned int i = 0; i < pool_size_; ++i) {
    BaseMapNode* node = AllocNewMapNode();
    InitNewMapNode(node);
    free_list_.push_back(node);
  }
}

void BaseMapNodePool::Release() {
  if (node_reset_workers_.valid()) {
    node_reset_workers_.get();
  }
  typename std::list<BaseMapNode*>::iterator i = free_list_.begin();
  while (i != free_list_.end()) {
    FinalizeMapNode(*i);
    DellocMapNode(*i);
    i++;
  }
  free_list_.clear();
  typename std::set<BaseMapNode*>::iterator j = busy_nodes_.begin();
  while (j != busy_nodes_.end()) {
    FinalizeMapNode(*j);
    DellocMapNode(*j);
    j++;
  }
  busy_nodes_.clear();
  pool_size_ = 0;
}

BaseMapNode* BaseMapNodePool::AllocMapNode() {
  if (free_list_.empty()) {
    if (node_reset_workers_.valid()) {
      node_reset_workers_.wait();
    }
  }
  boost::unique_lock<boost::mutex> lock(mutex_);
  if (free_list_.empty()) {
    if (is_fixed_size_) {
      return nullptr;
    }
    BaseMapNode* node = AllocNewMapNode();
    InitNewMapNode(node);
    pool_size_++;
    busy_nodes_.insert(node);
    return node;
  } else {
    BaseMapNode* node = free_list_.front();
    free_list_.pop_front();
    busy_nodes_.insert(node);
    return node;
  }
}

void BaseMapNodePool::FreeMapNode(BaseMapNode* map_node) {
  node_reset_workers_ =
      cyber::Async(&BaseMapNodePool::FreeMapNodeTask, this, map_node);
}

void BaseMapNodePool::FreeMapNodeTask(BaseMapNode* map_node) {
  FinalizeMapNode(map_node);
  ResetMapNode(map_node);
  {
    boost::unique_lock<boost::mutex> lock(mutex_);
    typename std::set<BaseMapNode*>::iterator f = busy_nodes_.find(map_node);
    DCHECK(f != busy_nodes_.end());
    free_list_.push_back(*f);
    busy_nodes_.erase(f);
  }
}

void BaseMapNodePool::InitNewMapNode(BaseMapNode* node) {
  node->InitMapMatrix(map_config_);
}

void BaseMapNodePool::FinalizeMapNode(BaseMapNode* node) { node->Finalize(); }

void BaseMapNodePool::DellocMapNode(BaseMapNode* node) { delete node; }

void BaseMapNodePool::ResetMapNode(BaseMapNode* node) { node->ResetMapNode(); }

}  // namespace msf
}  // namespace localization
}  // namespace apollo
