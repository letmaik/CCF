// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"
#include "ds/files.h"
#include "ds/logger.h"
#include "enclave/appinterface.h"
#include "node/clientsignatures.h"
#include "node/encryptor.h"
#include "node/genesisgen.h"
#include "node/rpc/jsonrpc.h"
#include "node/rpc/memberfrontend.h"
#include "node/rpc/userfrontend.h"
#include "node_stub.h"
#include "runtime_config/default_whitelists.h"

#include <iostream>
#include <string>

extern "C"
{
#include <evercrypt/EverCrypt_AutoConfig2.h>
}

using namespace ccfapp;
using namespace ccf;
using namespace std;
using namespace jsonrpc;
using namespace nlohmann;

// used throughout
auto kp = tls::make_key_pair();
auto ca_mem = kp -> self_sign("CN=name_member");
auto verifier_mem = tls::make_verifier(ca_mem);
auto member_caller = verifier_mem -> raw_cert_data();
auto encryptor = std::make_shared<ccf::NullTxEncryptor>();

string get_script_path(string name)
{
  auto default_dir = "../src/runtime_config";
  auto dir = getenv("RUNTIME_CONFIG_DIR");
  stringstream ss;
  ss << (dir ? dir : default_dir) << "/" << name;
  return ss.str();
}
const auto gov_script_file = files::slurp_string(get_script_path("gov.lua"));
const auto operator_gov_script_file =
  files::slurp_string(get_script_path("operator_gov.lua"));

template <typename T>
auto mpack(T&& a)
{
  return pack(forward<T>(a), Pack::MsgPack);
}

template <typename T>
auto munpack(T&& a)
{
  return unpack(forward<T>(a), Pack::MsgPack);
}

template <typename E>
void check_error(const nlohmann::json& j, const E expected)
{
  CHECK(
    j[ERR][CODE].get<jsonrpc::ErrorBaseType>() ==
    static_cast<jsonrpc::ErrorBaseType>(expected));
}

void check_success(const Response<bool> r, const bool expected = true)
{
  CHECK(r.result == expected);
}

void set_whitelists(GenesisGenerator& gen)
{
  for (const auto& wl : default_whitelists)
    gen.set_whitelist(wl.first, wl.second);
}

std::vector<uint8_t> sign_json(nlohmann::json j, tls::KeyPairPtr& kp_)
{
  auto contents = nlohmann::json::to_msgpack(j);
  return kp_->sign(contents);
}

json create_json_req(const json& params, const string& method_name)
{
  json j;
  j[JSON_RPC] = RPC_VERSION;
  j[ID] = 1;
  j[METHOD] = method_name;
  if (!params.is_null())
    j[PARAMS] = params;
  return j;
}

json create_json_req_signed(
  const json& params, const string& method_name, tls::KeyPairPtr& kp_)
{
  auto j = create_json_req(params, method_name);
  nlohmann::json sj;
  sj["req"] = j;
  auto sig = sign_json(j, kp_);
  sj["sig"] = sig;
  return sj;
}

template <typename T>
auto query_params(T script, bool compile)
{
  json params;
  if (compile)
    params["bytecode"] = lua::compile(script);
  else
    params["text"] = script;
  return params;
}

template <typename T>
auto read_params(const T& key, const string& table_name)
{
  json params;
  params["key"] = key;
  params["table"] = table_name;
  return params;
}

nlohmann::json get_proposal(
  enclave::RPCContext& rpc_ctx,
  MemberRpcFrontend& frontend,
  size_t proposal_id,
  CallerId as_member)
{
  Script read_proposal(fmt::format(
    R"xxx(
      tables = ...
      return tables["ccf.proposals"]:get({})
    )xxx",
    proposal_id));

  const auto readj = create_json_req(read_proposal, "query");

  Store::Tx tx;
  ccf::SignedReq sr(readj);
  return frontend.process_json(rpc_ctx, tx, as_member, readj, sr).value();
}

std::vector<uint8_t> get_cert_data(uint64_t member_id, tls::KeyPairPtr& kp_mem)
{
  std::vector<uint8_t> ca_mem =
    kp_mem->self_sign("CN=new member" + to_string(member_id));
  auto v_mem = tls::make_verifier(ca_mem);
  std::vector<uint8_t> cert_data = v_mem->raw_cert_data();
  return cert_data;
}

auto init_frontend(
  NetworkTables& network,
  GenesisGenerator& gen,
  StubNodeState& node,
  const int n_members)
{
  // create members with fake certs (no crypto here)
  for (uint8_t i = 0; i < n_members; i++)
    gen.add_member({i}, MemberStatus::ACTIVE);

  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();
  return MemberRpcFrontend(network, node);
}

TEST_CASE("Member query/read")
{
  // initialize the network state
  const Cert mcert = {0};
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  StubNodeState node;
  MemberRpcFrontend frontend(network, node);
  const auto mid = gen.add_member(mcert, MemberStatus::ACCEPTED);
  gen.finalize();
  enclave::RPCContext rpc_ctx(0, {});

  // put value to read
  constexpr auto key = 123;
  constexpr auto value = 456;
  Store::Tx tx;
  tx.get_view(network.values)->put(key, value);
  CHECK(tx.commit() == kv::CommitSuccess::OK);

  static constexpr auto query = R"xxx(
  local tables = ...
  return tables["ccf.values"]:get(123)
  )xxx";

  SUBCASE("Query: bytecode/script allowed access")
  {
    // set member ACL so that the VALUES table is accessible
    Store::Tx tx;
    tx.get_view(network.whitelists)
      ->put(WlIds::MEMBER_CAN_READ, {Tables::VALUES});
    CHECK(tx.commit() == kv::CommitSuccess::OK);

    bool compile = true;
    do
    {
      Store::Tx tx;
      auto req = create_json_req(query_params(query, compile), "query");
      ccf::SignedReq sr(req);

      auto rep = frontend.process_json(rpc_ctx, tx, mid, req, sr);
      CHECK(rep.has_value());
      const Response<int> r = rep.value();
      CHECK(r.result == value);
      compile = !compile;
    } while (!compile);
  }

  SUBCASE("Query: table not in ACL")
  {
    // set member ACL so that no table is accessible
    Store::Tx tx;
    tx.get_view(network.whitelists)->put(WlIds::MEMBER_CAN_READ, {});
    CHECK(tx.commit() == kv::CommitSuccess::OK);

    Store::Tx tx1;
    auto req = create_json_req(query_params(query, true), "query");
    ccf::SignedReq sr(req);

    check_error(
      frontend.process_json(rpc_ctx, tx1, 0, req, sr).value(),
      CCFErrorCodes::SCRIPT_ERROR);
  }

  SUBCASE("Read: allowed access, key exists")
  {
    Store::Tx tx;
    tx.get_view(network.whitelists)
      ->put(WlIds::MEMBER_CAN_READ, {Tables::VALUES});
    CHECK(tx.commit() == kv::CommitSuccess::OK);

    Store::Tx tx1;
    auto read_call_j =
      create_json_req(read_params<int>(key, Tables::VALUES), "read");
    ccf::SignedReq sr(read_call_j);

    auto response = frontend.process_json(rpc_ctx, tx1, mid, read_call_j, sr);
    Response<int> r = response.value();
    CHECK(r.result == value);
  }

  SUBCASE("Read: allowed access, key doesn't exist")
  {
    constexpr auto wrong_key = 321;
    Store::Tx tx;
    tx.get_view(network.whitelists)
      ->put(WlIds::MEMBER_CAN_READ, {Tables::VALUES});
    CHECK(tx.commit() == kv::CommitSuccess::OK);

    Store::Tx tx1;
    auto read_call_j =
      create_json_req(read_params<int>(wrong_key, Tables::VALUES), "read");
    ccf::SignedReq sr(read_call_j);

    check_error(
      frontend.process_json(rpc_ctx, tx1, mid, read_call_j, sr).value(),
      StandardErrorCodes::INVALID_PARAMS);
  }

  SUBCASE("Read: access not allowed")
  {
    Store::Tx tx;
    tx.get_view(network.whitelists)->put(WlIds::MEMBER_CAN_READ, {});
    CHECK(tx.commit() == kv::CommitSuccess::OK);

    Store::Tx tx1;
    auto read_call_j =
      create_json_req(read_params<int>(key, Tables::VALUES), "read");
    ccf::SignedReq sr(read_call_j);

    check_error(
      frontend.process_json(rpc_ctx, tx1, 0, read_call_j, sr).value(),
      CCFErrorCodes::SCRIPT_ERROR);
  }
}

TEST_CASE("Proposer ballot")
{
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();

  const auto proposer_cert = get_cert_data(0, kp);
  const auto proposer_id = gen.add_member(proposer_cert, MemberStatus::ACTIVE);
  const auto voter_cert = get_cert_data(1, kp);
  const auto voter_id = gen.add_member(voter_cert, MemberStatus::ACTIVE);

  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();

  StubNodeState node;
  MemberRpcFrontend frontend(network, node);

  size_t proposal_id;

  const ccf::Script vote_for("return true");
  const ccf::Script vote_against("return false");
  {
    INFO("Propose, initially voting against");

    const auto proposed_member = get_cert_data(2, kp);

    Script proposal(R"xxx(
      tables, member_cert = ...
      return Calls:call("new_member", member_cert)
    )xxx");
    const auto proposej = create_json_req(
      Propose::In{proposal, proposed_member, vote_against}, "propose");
    enclave::RPCContext rpc_ctx(proposer_id, proposer_cert);

    Store::Tx tx;
    ccf::SignedReq sr(proposej);
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, proposer_id, proposej, sr).value();

    // the proposal should be accepted, but not succeed immediately
    CHECK(r.result.completed == false);

    proposal_id = r.result.id;
  }

  {
    INFO("Second member votes for proposal");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(voter_id, voter_cert);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, voter_id, votej["req"], sr).value();

    // The vote should not yet succeed
    CHECK(r.result == false);
  }

  {
    INFO("Read current votes");

    const auto readj = create_json_req_signed(
      read_params(proposal_id, Tables::PROPOSALS), "read", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(proposer_id, proposer_cert);
    const Response<Proposal> proposal =
      get_proposal(rpc_ctx, frontend, proposal_id, proposer_id);

    const auto& votes = proposal.result.votes;
    CHECK(votes.size() == 2);

    const auto proposer_vote = votes.find(proposer_id);
    CHECK(proposer_vote != votes.end());
    CHECK(proposer_vote->second == vote_against);

    const auto voter_vote = votes.find(voter_id);
    CHECK(voter_vote != votes.end());
    CHECK(voter_vote->second == vote_for);
  }

  {
    INFO("Proposer votes for");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(proposer_id, proposer_cert);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, proposer_id, votej["req"], sr).value();

    // The vote should now succeed
    CHECK(r.result == true);
  }
}

struct NewMember
{
  MemberId id;
  tls::KeyPairPtr kp = tls::make_key_pair();
  Cert cert;
};

TEST_CASE("Add new members until there are 7, then reject")
{
  constexpr auto initial_members = 3;
  constexpr auto n_new_members = 7;
  constexpr auto max_members = 8;
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  StubNodeState node;
  // add three initial active members
  // the proposer
  auto proposer_id =
    gen.add_member(vector<uint8_t>(member_caller), MemberStatus::ACTIVE);

  // the voters
  auto voter_a = gen.add_member(get_cert_data(1, kp), MemberStatus::ACTIVE);
  auto voter_b = gen.add_member(get_cert_data(2, kp), MemberStatus::ACTIVE);

  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();
  MemberRpcFrontend frontend(network, node);

  vector<NewMember> new_members(n_new_members);

  auto i = 0ul;
  for (auto& new_member : new_members)
  {
    const auto proposal_id = i;
    new_member.id = initial_members + i++;

    // new member certificate
    auto v = tls::make_verifier(
      new_member.kp->self_sign(fmt::format("CN=new member{}", new_member.id)));
    const auto _cert = v->raw();
    new_member.cert = {_cert->raw.p, _cert->raw.p + _cert->raw.len};

    // check new_member id does not work before member is added
    enclave::RPCContext rpc_ctx(0, new_member.cert);
    const auto read_next_member_id = mpack(create_json_req(
      read_params<int>(ValueIds::NEXT_MEMBER_ID, Tables::VALUES), "read"));
    check_error(
      munpack(frontend.process(rpc_ctx, read_next_member_id)),
      CCFErrorCodes::INVALID_CALLER_ID);

    // propose new member, as proposer
    Script proposal(R"xxx(
      local tables, member_cert = ...
      return Calls:call("new_member", member_cert)
    )xxx");

    const auto proposej =
      create_json_req(Propose::In{proposal, new_member.cert}, "propose");

    {
      Store::Tx tx;
      ccf::SignedReq sr(proposej);
      Response<Propose::Out> r =
        frontend.process_json(rpc_ctx, tx, proposer_id, proposej, sr).value();

      // the proposal should be accepted, but not succeed immediately
      CHECK(r.result.id == proposal_id);
      CHECK(r.result.completed == false);
    }

    // read initial proposal, as second member
    const Response<Proposal> initial_read =
      get_proposal(rpc_ctx, frontend, proposal_id, voter_a);
    CHECK(initial_read.result.proposer == proposer_id);
    CHECK(initial_read.result.script == proposal);
    CHECK(initial_read.result.parameter == new_member.cert);

    // vote as second member
    Script vote_ballot(fmt::format(
      R"xxx(
        local tables, calls = ...
        local n = 0
        tables["ccf.members"]:foreach( function(k, v) n = n + 1 end )
        if n < {} then
          return true
        else
          return false
        end
      )xxx",
      max_members));

    json votej =
      create_json_req_signed(Vote{proposal_id, vote_ballot}, "vote", kp);

    {
      Store::Tx tx;
      enclave::RPCContext mem_rpc_ctx(0, member_caller);
      ccf::SignedReq sr(votej);
      Response<bool> r =
        frontend.process_json(mem_rpc_ctx, tx, voter_a, votej["req"], sr)
          .value();

      if (new_member.id < max_members)
      {
        // vote should succeed
        CHECK(r.result);
        // check that member with the new new_member cert can make rpc's now
        CHECK(
          Response<int>(munpack(frontend.process(rpc_ctx, read_next_member_id)))
            .result == new_member.id + 1);

        // successful proposals are removed from the kv, so we can't confirm
        // their final state
      }
      else
      {
        // vote should not succeed
        CHECK(!r.result);
        // check that member with the new new_member cert can make rpc's now
        check_error(
          munpack(frontend.process(rpc_ctx, read_next_member_id)),
          CCFErrorCodes::INVALID_CALLER_ID);

        // re-read proposal, as second member
        const Response<Proposal> final_read =
          get_proposal(rpc_ctx, frontend, proposal_id, voter_a);
        CHECK(final_read.result.proposer == proposer_id);
        CHECK(final_read.result.script == proposal);
        CHECK(final_read.result.parameter == new_member.cert);

        const auto my_vote = final_read.result.votes.find(voter_a);
        CHECK(my_vote != final_read.result.votes.end());
        CHECK(my_vote->second == vote_ballot);
      }
    }
  }

  SUBCASE("ACK from newly added members")
  {
    // iterate over all new_members, except for the last one
    for (auto new_member = new_members.cbegin(); new_member !=
         new_members.cend() - (initial_members + n_new_members - max_members);
         new_member++)
    {
      enclave::RPCContext rpc_ctx(0, new_member->cert);

      // (1) read ack entry
      const auto read_nonce = mpack(create_json_req(
        read_params(new_member->id, Tables::MEMBER_ACKS), "read"));
      const Response<MemberAck> ack0 =
        munpack(frontend.process(rpc_ctx, read_nonce));
      // (2) ask for a fresher nonce
      const auto freshen_nonce =
        mpack(create_json_req(nullptr, "updateAckNonce"));
      check_success(munpack(frontend.process(rpc_ctx, freshen_nonce)));
      // (3) read ack entry again and check that the nonce has changed
      const Response<MemberAck> ack1 =
        munpack(frontend.process(rpc_ctx, read_nonce));
      CHECK(ack0.result.next_nonce != ack1.result.next_nonce);
      // (4) sign old nonce and send it
      const auto bad_sig =
        RawSignature{new_member->kp->sign(ack0.result.next_nonce)};
      const auto send_bad_sig = mpack(create_json_req(bad_sig, "ack"));
      check_error(
        munpack(frontend.process(rpc_ctx, send_bad_sig)),
        jsonrpc::StandardErrorCodes::INVALID_PARAMS);
      // (5) sign new nonce and send it
      const auto good_sig =
        RawSignature{new_member->kp->sign(ack1.result.next_nonce)};
      const auto send_good_sig = mpack(create_json_req(good_sig, "ack"));
      check_success(munpack(frontend.process(rpc_ctx, send_good_sig)));
      // (6) read ack entry again and check that the signature matches
      const Response<MemberAck> ack2 =
        munpack(frontend.process(rpc_ctx, read_nonce));
      CHECK(ack2.result.sig == good_sig.sig);
      // (7) read own member status
      const auto read_status = mpack(
        create_json_req(read_params(new_member->id, Tables::MEMBERS), "read"));
      const Response<MemberInfo> mi =
        munpack(frontend.process(rpc_ctx, read_status));
      CHECK(mi.result.status == MemberStatus::ACTIVE);
    }
  }
}

TEST_CASE("Accept node")
{
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  StubNodeState node;
  auto new_kp = tls::make_key_pair();

  const Cert mcert0 = get_cert_data(0, new_kp), mcert1 = get_cert_data(1, kp);
  const auto mid0 = gen.add_member(mcert0, MemberStatus::ACTIVE);
  const auto mid1 = gen.add_member(mcert1, MemberStatus::ACTIVE);
  enclave::RPCContext rpc_ctx(0, mcert1);

  // node to be tested
  // new node certificate
  auto new_ca = new_kp->self_sign("CN=new node");
  NodeInfo ni;
  ni.cert = new_ca;
  gen.add_node(ni);
  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();
  MemberRpcFrontend frontend(network, node);
  auto node_id = 0;
  // check node exists with status pending
  {
    Store::Tx tx;
    auto read_values_j =
      create_json_req(read_params<int>(node_id, Tables::NODES), "read");
    ccf::SignedReq sr(read_values_j);

    Response<NodeInfo> r =
      frontend.process_json(rpc_ctx, tx, mid0, read_values_j, sr).value();
    CHECK(r.result.status == NodeStatus::PENDING);
  }
  // m0 proposes adding new node
  {
    Script proposal(R"xxx(
      local tables, node_id = ...
      return Calls:call("trust_node", node_id)
    )xxx");

    json proposej = create_json_req(Propose::In{proposal, node_id}, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, mid0, proposej, sr).value();
    CHECK(!r.result.completed);
    CHECK(r.result.id == 0);
  }
  // m1 votes for accepting a single new node
  {
    Script vote_ballot(R"xxx(
        local tables, calls = ...
        return #calls == 1 and calls[1].func == "trust_node"
       )xxx");

    json votej = create_json_req_signed(Vote{0, vote_ballot}, "vote", kp);
    ccf::SignedReq sr(votej);

    Store::Tx tx;
    check_success(
      frontend.process_json(rpc_ctx, tx, mid1, votej["req"], sr).value());
  }
  // check node exists with status pending
  {
    Store::Tx tx;
    auto read_values_j =
      create_json_req(read_params<int>(node_id, Tables::NODES), "read");
    ccf::SignedReq sr(read_values_j);

    Response<NodeInfo> r =
      frontend.process_json(rpc_ctx, tx, mid0, read_values_j, sr).value();
    CHECK(r.result.status == NodeStatus::TRUSTED);
  }
}

bool test_raw_writes(
  NetworkTables& network,
  GenesisGenerator& gen,
  StubNodeState& node,
  Propose::In proposal,
  const int n_members = 1,
  const int pro_votes = 1,
  bool explicit_proposer_vote = false)
{
  enclave::RPCContext rpc_ctx(0, {});
  auto frontend = init_frontend(network, gen, node, n_members);
  // check values before
  {
    Store::Tx tx;
    auto next_member_id_r =
      tx.get_view(network.values)->get(ValueIds::NEXT_MEMBER_ID);
    CHECK(next_member_id_r);
    CHECK(*next_member_id_r == n_members);
  }
  // propose
  const auto proposal_id = 0ul;
  {
    const uint8_t proposer_id = 0;
    json proposej = create_json_req(proposal, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, proposer_id, proposej, sr).value();
    CHECK(r.result.completed == (n_members == 1));
    CHECK(r.result.id == proposal_id);
    if (r.result.completed)
      return true;
  }
  // con votes
  for (int i = n_members - 1; i >= pro_votes; i--)
  {
    auto mem_cert = get_cert_data(i, kp);
    enclave::RPCContext mem_rpc_ctx(0, mem_cert);
    const Script vote("return false");
    json votej = create_json_req_signed(Vote{proposal_id, vote}, "vote", kp);
    ccf::SignedReq sr(votej);

    Store::Tx tx;
    check_success(
      frontend.process_json(mem_rpc_ctx, tx, i, votej["req"], sr).value(),
      false);
  }
  // pro votes (proposer also votes)
  bool completed = false;
  for (uint8_t i = explicit_proposer_vote ? 0 : 1; i < pro_votes; i++)
  {
    const Script vote("return true");
    json votej = create_json_req_signed(Vote{proposal_id, vote}, "vote", kp);
    ccf::SignedReq sr(votej);

    Store::Tx tx;
    auto mem_cert = get_cert_data(i, kp);
    enclave::RPCContext mem_rpc_ctx(0, mem_cert);
    if (!completed)
    {
      completed =
        Response<bool>(
          frontend.process_json(mem_rpc_ctx, tx, i, votej["req"], sr).value())
          .result;
    }
    else
    {
      // proposal has been accepted - additional votes return an error
      check_error(
        frontend.process_json(mem_rpc_ctx, tx, i, votej["req"], sr).value(),
        StandardErrorCodes::INVALID_PARAMS);
    }
  }
  return completed;
}

TEST_CASE("Propose raw writes")
{
  SUBCASE("insensitive tables")
  {
    const auto n_members = 10;
    for (int pro_votes = 0; pro_votes <= n_members; pro_votes++)
    {
      const bool should_succeed = pro_votes > n_members / 2;
      NetworkTables network;
      Store::Tx gen_tx;
      GenesisGenerator gen(network, gen_tx);
      gen.init_values();
      StubNodeState node;
      // manually add a member in state active (not recommended)
      const Cert mcert = {1, 2, 3};
      CHECK(
        test_raw_writes(
          network,
          gen,
          node,
          {R"xxx(
        local tables, cert = ...
        local STATE_ACTIVE = 1
        local NEXT_MEMBER_ID_VALUE = 0
        local p = Puts:new()
        -- get id
        local member_id = tables["ccf.values"]:get(NEXT_MEMBER_ID_VALUE)
        -- increment id
        p:put("ccf.values", NEXT_MEMBER_ID_VALUE, member_id + 1)
        -- write member cert and status
        p:put("ccf.members", member_id, {cert = cert, status = STATE_ACTIVE})
        p:put("ccf.member_certs", cert, member_id)
        return Calls:call("raw_puts", p)
      )xxx"s,
           mcert},
          n_members,
          pro_votes) == should_succeed);
      if (!should_succeed)
        continue;

      // check results
      Store::Tx tx;
      const auto next_mid =
        tx.get_view(network.values)->get(ValueIds::NEXT_MEMBER_ID);
      CHECK(next_mid);
      CHECK(*next_mid == n_members + 1);
      const auto m = tx.get_view(network.members)->get(n_members);
      CHECK(m);
      CHECK(m->status == MemberStatus::ACTIVE);
      const auto mid = tx.get_view(network.member_certs)->get(mcert);
      CHECK(mid);
      CHECK(*mid == n_members);
    }
  }

  SUBCASE("sensitive tables")
  {
    // propose changes to sensitive tables; changes must only be accepted
    // unanimously create new network for each case
    const auto sensitive_tables = {Tables::WHITELISTS, Tables::GOV_SCRIPTS};
    const auto n_members = 10;
    // let proposer vote/not vote
    for (const auto proposer_vote : {true, false})
    {
      for (int pro_votes = 0; pro_votes < n_members; pro_votes++)
      {
        for (const auto& sensitive_table : sensitive_tables)
        {
          NetworkTables network;
          Store::Tx gen_tx;
          GenesisGenerator gen(network, gen_tx);
          gen.init_values();
          StubNodeState node;

          const auto sensitive_put =
            "return Calls:call('raw_puts', Puts:put('"s + sensitive_table +
            "', 9, {'aaa'}))"s;
          CHECK(
            test_raw_writes(
              network,
              gen,
              node,
              {sensitive_put},
              n_members,
              pro_votes,
              proposer_vote) == (n_members == pro_votes));
        }
      }
    }
  }
}

TEST_CASE("Remove proposal")
{
  NewMember caller;
  auto v = tls::make_verifier(caller.kp->self_sign("CN=new member"));
  caller.cert = v->raw_cert_data();

  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();

  StubNodeState node;
  enclave::RPCContext rpc_ctx(0, {});
  gen.add_member(member_caller, MemberStatus::ACTIVE);
  gen.add_member(caller.cert, MemberStatus::ACTIVE);
  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();
  MemberRpcFrontend frontend(network, node);
  auto proposal_id = 0;
  auto wrong_proposal_id = 1;
  ccf::Script proposal_script(R"xxx(
      local tables, param = ...
      return {}
    )xxx");

  // check that the proposal doesn't exist
  {
    Store::Tx tx;
    auto proposal = tx.get_view(network.proposals)->get(proposal_id);
    CHECK(!proposal);
  }

  {
    json proposej = create_json_req(Propose::In{proposal_script, 0}, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, 0, proposej, sr).value();
    CHECK(r.result.id == proposal_id);
    CHECK(!r.result.completed);
  }
  // check that the proposal is there
  {
    Store::Tx tx;
    auto proposal = tx.get_view(network.proposals)->get(proposal_id);
    REQUIRE(proposal);
    CHECK(proposal->state == ProposalState::OPEN);
    CHECK(proposal->script.text.value() == proposal_script.text.value());
  }
  SUBCASE("Attempt withdraw proposal with non existing id")
  {
    Store::Tx tx;
    json param;
    param["id"] = wrong_proposal_id;
    json withdrawj = create_json_req(param, "withdraw");
    ccf::SignedReq sr(withdrawj);

    check_error(
      frontend.process_json(rpc_ctx, tx, 0, withdrawj, sr).value(),
      StandardErrorCodes::INVALID_PARAMS);
  }
  SUBCASE("Attempt withdraw proposal that you didn't propose")
  {
    Store::Tx tx;
    json param;
    param["id"] = proposal_id;
    json withdrawj = create_json_req(param, "withdraw");
    ccf::SignedReq sr(withdrawj);

    check_error(
      frontend.process_json(rpc_ctx, tx, 1, withdrawj, sr).value(),
      CCFErrorCodes::INVALID_CALLER_ID);
  }
  SUBCASE("Successfully withdraw proposal")
  {
    Store::Tx tx;
    json param;
    param["id"] = proposal_id;
    json withdrawj = create_json_req(param, "withdraw");
    ccf::SignedReq sr(withdrawj);

    check_success(frontend.process_json(rpc_ctx, tx, 0, withdrawj, sr).value());
    // check that the proposal is now withdrawn
    {
      Store::Tx tx;
      auto proposal = tx.get_view(network.proposals)->get(proposal_id);
      CHECK(proposal.has_value());
      CHECK(proposal->state == ProposalState::WITHDRAWN);
    }
  }
}

TEST_CASE("Complete proposal after initial rejection")
{
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  StubNodeState node;
  auto frontend = init_frontend(network, gen, node, 3);
  const Cert m0 = {0}, m1 = get_cert_data(1, kp);
  enclave::RPCContext rpc_ctx(0, m1);
  // propose
  {
    const auto proposal =
      "return Calls:call('raw_puts', Puts:put('ccf.values', 999, 999))"s;
    const auto proposej = create_json_req(Propose::In{proposal}, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, 0, proposej, sr).value();
    CHECK(r.result.completed == false);
  }
  // vote that rejects initially
  {
    const Script vote(R"xxx(
    local tables = ...
    return tables["ccf.values"]:get(123) == 123
    )xxx");
    const auto votej = create_json_req_signed(Vote{0, vote}, "vote", kp);
    ccf::SignedReq sr(votej);

    Store::Tx tx;
    check_success(
      frontend.process_json(rpc_ctx, tx, 1, votej["req"], sr).value(), false);
  }
  // try to complete
  {
    const auto completej = create_json_req(ProposalAction{0}, "complete");
    ccf::SignedReq sr(completej);

    Store::Tx tx;
    check_success(
      frontend.process_json(rpc_ctx, tx, 1, completej, sr).value(), false);
  }
  // put value that makes vote agree
  {
    Store::Tx tx;
    tx.get_view(network.values)->put(123, 123);
    CHECK(tx.commit() == kv::CommitSuccess::OK);
  }
  // try again to complete
  {
    const auto completej = create_json_req(ProposalAction{0}, "complete");
    ccf::SignedReq sr(completej);

    Store::Tx tx;
    check_success(frontend.process_json(rpc_ctx, tx, 1, completej, sr).value());
  }
}

TEST_CASE("Add user via proposed call")
{
  NetworkTables network;
  network.tables->set_encryptor(encryptor);
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  StubNodeState node;
  enclave::RPCContext rpc_ctx(0, {});
  gen.add_member(Cert{0}, MemberStatus::ACTIVE);
  set_whitelists(gen);
  gen.set_gov_scripts(lua::Interpreter().invoke<json>(gov_script_file));
  gen.finalize();
  MemberRpcFrontend frontend(network, node);

  Script proposal(R"xxx(
    tables, user_cert = ...
      return Calls:call("new_user", user_cert)
    )xxx");

  const vector<uint8_t> user_cert = {1, 2, 3};
  json proposej = create_json_req(Propose::In{proposal, user_cert}, "propose");
  ccf::SignedReq sr(proposej);

  Store::Tx tx;
  Response<Propose::Out> r =
    frontend.process_json(rpc_ctx, tx, 0, proposej, sr).value();
  CHECK(r.result.completed);
  CHECK(r.result.id == 0);

  Store::Tx tx1;
  const auto uid = tx1.get_view(network.values)->get(ValueIds::NEXT_USER_ID);
  REQUIRE(uid);
  CHECK(*uid == 1);
  const auto uid1 = tx1.get_view(network.user_certs)->get(user_cert);
  REQUIRE(uid1);
  CHECK(*uid1 == 0);
}

TEST_CASE("Passing members ballot with operator")
{
  // Members pass a ballot with a constitution that includes an operator
  // Operator votes, but is _not_ taken into consideration
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();

  // Operating member, as set in operator_gov.lua
  const auto operator_cert = get_cert_data(0, kp);
  const auto operator_id = gen.add_member(operator_cert, MemberStatus::ACTIVE);

  // Non-operating members
  std::map<size_t, ccf::Cert> members;
  for (size_t i = 1; i < 4; i++)
  {
    auto cert = get_cert_data(i, kp);
    members[gen.add_member(cert, MemberStatus::ACTIVE)] = cert;
  }

  set_whitelists(gen);
  gen.set_gov_scripts(
    lua::Interpreter().invoke<json>(operator_gov_script_file));
  gen.finalize();

  StubNodeState node;
  MemberRpcFrontend frontend(network, node);

  size_t proposal_id;
  size_t proposer_id = 1;
  size_t voter_id = 2;

  const ccf::Script vote_for("return true");
  const ccf::Script vote_against("return false");
  {
    INFO("Propose and vote for");

    const auto proposed_member = get_cert_data(4, kp);

    Script proposal(R"xxx(
      tables, member_cert = ...
      return Calls:call("new_member", member_cert)
    )xxx");
    const auto proposej = create_json_req(
      Propose::In{proposal, proposed_member, vote_for}, "propose");
    enclave::RPCContext rpc_ctx(proposer_id, members[proposer_id]);

    Store::Tx tx;
    ccf::SignedReq sr(proposej);
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, proposer_id, proposej, sr).value();

    CHECK(r.result.completed == false);

    proposal_id = r.result.id;
  }

  {
    INFO("Operator votes, but without effect");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, operator_id, votej["req"], sr).value();

    CHECK(r.result == false);
  }

  {
    INFO("Second member votes for proposal, which passes");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(voter_id, members[voter_id]);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, voter_id, votej["req"], sr).value();

    CHECK(r.result == true);
  }

  {
    INFO("Validate vote tally");

    const auto readj = create_json_req_signed(
      read_params(proposal_id, Tables::PROPOSALS), "read", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(proposer_id, members[proposer_id]);
    const Response<Proposal> proposal =
      get_proposal(rpc_ctx, frontend, proposal_id, proposer_id);

    const auto& votes = proposal.result.votes;
    CHECK(votes.size() == 3);

    const auto operator_vote = votes.find(operator_id);
    CHECK(operator_vote != votes.end());
    CHECK(operator_vote->second == vote_for);

    const auto proposer_vote = votes.find(proposer_id);
    CHECK(proposer_vote != votes.end());
    CHECK(proposer_vote->second == vote_for);

    const auto voter_vote = votes.find(voter_id);
    CHECK(voter_vote != votes.end());
    CHECK(voter_vote->second == vote_for);
  }
}

TEST_CASE("Passing operator vote")
{
  // Operator issues a proposal that only requires its own vote
  // and gets it through without member votes
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  auto new_kp = tls::make_key_pair();
  auto new_ca = new_kp->self_sign("CN=new node");
  NodeInfo ni;
  ni.cert = new_ca;
  gen.add_node(ni);

  // Operating member, as set in operator_gov.lua
  const auto operator_cert = get_cert_data(0, kp);
  const auto operator_id = gen.add_member(operator_cert, MemberStatus::ACTIVE);

  // Non-operating members
  std::map<size_t, ccf::Cert> members;
  for (size_t i = 1; i < 4; i++)
  {
    auto cert = get_cert_data(i, kp);
    members[gen.add_member(cert, MemberStatus::ACTIVE)] = cert;
  }

  set_whitelists(gen);
  gen.set_gov_scripts(
    lua::Interpreter().invoke<json>(operator_gov_script_file));
  gen.finalize();

  StubNodeState node;
  MemberRpcFrontend frontend(network, node);

  size_t proposal_id;

  const ccf::Script vote_for("return true");
  const ccf::Script vote_against("return false");

  auto node_id = 0;
  {
    INFO("Check node exists with status pending");
    Store::Tx tx;
    auto read_values_j =
      create_json_req(read_params<int>(node_id, Tables::NODES), "read");
    ccf::SignedReq sr(read_values_j);

    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    Response<NodeInfo> r =
      frontend.process_json(rpc_ctx, tx, operator_id, read_values_j, sr)
        .value();
    CHECK(r.result.status == NodeStatus::PENDING);
  }

  {
    INFO("Operator proposes and votes for node");
    Script proposal(R"xxx(
      local tables, node_id = ...
      return Calls:call("trust_node", node_id)
    )xxx");

    json proposej =
      create_json_req(Propose::In{proposal, node_id, vote_for}, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, operator_id, proposej, sr).value();

    CHECK(r.result.completed);
    proposal_id = r.result.id;
  }

  {
    INFO("Validate vote tally");

    const auto readj = create_json_req_signed(
      read_params(proposal_id, Tables::PROPOSALS), "read", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    const Response<Proposal> proposal =
      get_proposal(rpc_ctx, frontend, proposal_id, 1);

    const auto& votes = proposal.result.votes;
    CHECK(votes.size() == 1);

    const auto proposer_vote = votes.find(operator_id);
    CHECK(proposer_vote != votes.end());
    CHECK(proposer_vote->second == vote_for);
  }
}

TEST_CASE("Members passing an operator vote")
{
  // Operator proposes a vote, but does not vote for it
  // A majority of members pass the vote
  NetworkTables network;
  Store::Tx gen_tx;
  GenesisGenerator gen(network, gen_tx);
  gen.init_values();
  auto new_kp = tls::make_key_pair();
  auto new_ca = new_kp->self_sign("CN=new node");
  NodeInfo ni;
  ni.cert = new_ca;
  gen.add_node(ni);

  // Operating member, as set in operator_gov.lua
  const auto operator_cert = get_cert_data(0, kp);
  const auto operator_id = gen.add_member(operator_cert, MemberStatus::ACTIVE);

  // Non-operating members
  std::map<size_t, ccf::Cert> members;
  for (size_t i = 1; i < 4; i++)
  {
    auto cert = get_cert_data(i, kp);
    members[gen.add_member(cert, MemberStatus::ACTIVE)] = cert;
  }

  set_whitelists(gen);
  gen.set_gov_scripts(
    lua::Interpreter().invoke<json>(operator_gov_script_file));
  gen.finalize();

  StubNodeState node;
  MemberRpcFrontend frontend(network, node);

  size_t proposal_id;

  const ccf::Script vote_for("return true");
  const ccf::Script vote_against("return false");

  auto node_id = 0;
  {
    INFO("Check node exists with status pending");
    Store::Tx tx;
    auto read_values_j =
      create_json_req(read_params<int>(node_id, Tables::NODES), "read");
    ccf::SignedReq sr(read_values_j);

    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    Response<NodeInfo> r =
      frontend.process_json(rpc_ctx, tx, operator_id, read_values_j, sr)
        .value();
    CHECK(r.result.status == NodeStatus::PENDING);
  }

  {
    INFO("Operator proposes and votes against adding node");
    Script proposal(R"xxx(
      local tables, node_id = ...
      return Calls:call("trust_node", node_id)
    )xxx");

    json proposej =
      create_json_req(Propose::In{proposal, node_id, vote_against}, "propose");
    ccf::SignedReq sr(proposej);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    Response<Propose::Out> r =
      frontend.process_json(rpc_ctx, tx, operator_id, proposej, sr).value();

    CHECK(!r.result.completed);
    proposal_id = r.result.id;
  }

  size_t first_voter_id = 1;
  size_t second_voter_id = 2;

  {
    INFO("First member votes for proposal");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(first_voter_id, members[first_voter_id]);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, first_voter_id, votej["req"], sr)
        .value();

    CHECK(r.result == false);
  }

  {
    INFO("Second member votes for proposal");

    const auto votej =
      create_json_req_signed(Vote{proposal_id, vote_for}, "vote", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(second_voter_id, members[second_voter_id]);
    ccf::SignedReq sr(votej);
    Response<bool> r =
      frontend.process_json(rpc_ctx, tx, second_voter_id, votej["req"], sr)
        .value();

    CHECK(r.result == true);
  }

  {
    INFO("Validate vote tally");

    const auto readj = create_json_req_signed(
      read_params(proposal_id, Tables::PROPOSALS), "read", kp);

    Store::Tx tx;
    enclave::RPCContext rpc_ctx(operator_id, operator_cert);
    const Response<Proposal> proposal =
      get_proposal(rpc_ctx, frontend, proposal_id, 1);

    const auto& votes = proposal.result.votes;
    CHECK(votes.size() == 3);

    const auto proposer_vote = votes.find(operator_id);
    CHECK(proposer_vote != votes.end());
    CHECK(proposer_vote->second == vote_against);

    const auto first_vote = votes.find(first_voter_id);
    CHECK(first_vote != votes.end());
    CHECK(first_vote->second == vote_for);

    const auto second_vote = votes.find(second_voter_id);
    CHECK(second_vote != votes.end());
    CHECK(second_vote->second == vote_for);
  }
}

// We need an explicit main to initialize kremlib and EverCrypt
int main(int argc, char** argv)
{
  doctest::Context context;
  context.applyCommandLine(argc, argv);
  ::EverCrypt_AutoConfig2_init();
  int res = context.run();
  if (context.shouldExit())
    return res;
  return res;
}