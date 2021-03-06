// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "crypto/symmkey.h"
#include "kv/kvtypes.h"

#include <algorithm>
#include <iostream>

namespace kv
{
  class StubConsensus : public Consensus
  {
  private:
    std::vector<std::vector<uint8_t>> replica;

  public:
    StubConsensus() : Consensus(0), replica() {}

    bool replicate(
      const std::vector<std::tuple<SeqNo, std::vector<uint8_t>, bool>>& entries)
      override
    {
      for (auto&& [index, data, globally_committable] : entries)
      {
        replica.push_back(data);
      }
      return true;
    }

    std::pair<std::vector<uint8_t>, bool> get_latest_data()
    {
      if (!replica.empty())
        return std::make_pair(replica.back(), true);
      else
        return std::make_pair(std::vector<uint8_t>(), false);
    }

    size_t number_of_replicas()
    {
      return replica.size();
    }

    void flush()
    {
      replica.clear();
    }

    View get_view() override
    {
      return 0;
    }

    SeqNo get_commit_seqno() override
    {
      return 0;
    }

    NodeId primary() override
    {
      return 1;
    }

    NodeId id() override
    {
      return 0;
    }

    View get_view(SeqNo seqno) override
    {
      return 2;
    }

    void recv_message(const uint8_t* data, size_t size) override {}

    void add_configuration(
      SeqNo seqno,
      std::unordered_set<NodeId> conf,
      const NodeConf& node_conf) override
    {}

    void set_f(ccf::NodeId) override
    {
      return;
    }
  };

  class BackupStubConsensus : public StubConsensus
  {
  public:
    bool is_primary() override
    {
      return false;
    }

    bool replicate(
      const std::vector<std::tuple<SeqNo, std::vector<uint8_t>, bool>>& entries)
      override
    {
      return false;
    }
  };

  class PrimaryStubConsensus : public StubConsensus
  {
  public:
    bool is_primary() override
    {
      return true;
    }
  };
}
