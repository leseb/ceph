// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "mgr/ServiceMap.h"

#include <experimental/iterator>
#include <fmt/format.h>
#include <regex>

#include "common/Formatter.h"

using ceph::bufferlist;
using ceph::Formatter;

// Daemon

void ServiceMap::Daemon::encode(bufferlist& bl, uint64_t features) const
{
  ENCODE_START(2, 1, bl);
  encode(gid, bl);
  encode(addr, bl, features);
  encode(start_epoch, bl);
  encode(start_stamp, bl);
  encode(metadata, bl);
  encode(task_status, bl);
  ENCODE_FINISH(bl);
}

void ServiceMap::Daemon::decode(bufferlist::const_iterator& p)
{
  DECODE_START(2, p);
  decode(gid, p);
  decode(addr, p);
  decode(start_epoch, p);
  decode(start_stamp, p);
  decode(metadata, p);
  if (struct_v >= 2) {
    decode(task_status, p);
  }
  DECODE_FINISH(p);
}

void ServiceMap::Daemon::dump(Formatter *f) const
{
  f->dump_unsigned("start_epoch", start_epoch);
  f->dump_stream("start_stamp") << start_stamp;
  f->dump_unsigned("gid", gid);
  f->dump_string("addr", addr.get_legacy_str());
  f->open_object_section("metadata");
  for (auto& p : metadata) {
    f->dump_string(p.first.c_str(), p.second);
  }
  f->close_section();
  f->open_object_section("task_status");
  for (auto& p : task_status) {
    f->dump_string(p.first.c_str(), p.second);
  }
  f->close_section();
}

void ServiceMap::Daemon::generate_test_instances(std::list<Daemon*>& ls)
{
  ls.push_back(new Daemon);
  ls.push_back(new Daemon);
  ls.back()->gid = 222;
  ls.back()->metadata["this"] = "that";
  ls.back()->task_status["task1"] = "running";
}

// Service

std::string ServiceMap::Service::get_summary() const
{
  if (!summary.empty()) {
    return summary;
  }
  if (daemons.empty()) {
    return "no daemons active";
  }

  std::ostringstream ss;

  // The format two pairs in metadata:
  //   "daemon_type"   : "${TYPE}"
  //   "daemon_prefix" : "${PREFIX}"
  //
  // TYPE: will be used to replace the default "daemon(s)"
  // showed in `ceph -s`. If absent, the "daemon" will be used.
  // PREFIX: if present the active members will be classified
  // by the prefix instead of "daemon_name".
  //
  // For exmaple for iscsi gateways, it will be something likes:
  //   "daemon_type"   : "portal"
  //   "daemon_prefix" : "gateway${N}"
  // The `ceph -s` will be something likes:
  //    iscsi: 3 portals active (gateway0, gateway1, gateway2)

  std::map<std::string, std::set<std::string>> prefs;
  for (auto& d : daemons) {
    // In case the "daemon_type" is absent, use the
    // default "daemon" type
    std::string type("daemon");
    std::string prefix;

    auto t = d.second.metadata.find("daemon_type");
    if (t != d.second.metadata.end()) {
      type = d.second.metadata.at("daemon_type");
    }
    auto p = d.second.metadata.find("daemon_prefix");
    if (p != d.second.metadata.end()) {
      prefix = d.second.metadata.at("daemon_prefix");
    } else {
      // In case the "daemon_prefix" is absent, show
      // the daemon_name instead.
      prefix = d.first;
    }
    auto& pref = prefs[type];
    pref.insert(prefix);
  }

  for (auto &pr : prefs) {
    if (!ss.str().empty())
      ss << ", ";

    ss << pr.second.size() << " " << pr.first
       << (pr.second.size() > 1 ? "s" : "")
       << " active";

    if (pr.second.size()) {
      ss << " (";
      std::copy(std::begin(pr.second), std::end(pr.second),
                std::experimental::make_ostream_joiner(ss, ", "));
      ss << ")";
    }
  }

  return ss.str();
}

bool ServiceMap::Service::has_running_tasks() const
{
  return std::any_of(daemons.begin(), daemons.end(), [](auto& daemon) {
    return !daemon.second.task_status.empty();
  });
}

std::string ServiceMap::Service::get_task_summary(const std::string_view task_prefix) const
{
  // contruct a map similar to:
  //     {"service1 status" -> {"service1.0" -> "running"}}
  //     {"service2 status" -> {"service2.0" -> "idle"},
  //                           {"service2.1" -> "running"}}
  std::map<std::string, std::map<std::string, std::string>> by_task;
  for (const auto& [service_id, daemon] : daemons) {
    for (const auto& [task_name, status] : daemon.task_status) {
      by_task[task_name].emplace(fmt::format("{}.{}", task_prefix, service_id),
				 status);
    }
  }
  std::stringstream ss;
  for (const auto &[task_name, status_by_service] : by_task) {
    ss << "\n    " << task_name << ":";
    for (auto& [service, status] : status_by_service) {
      ss << "\n        " << service << ": " << status;
    }
  }
  return ss.str();
}

void ServiceMap::Service::count_metadata(const std::string& field,
					std::map<std::string,int> *out) const
{
  for (auto& p : daemons) {
    auto q = p.second.metadata.find(field);
    if (q == p.second.metadata.end()) {
      (*out)["unknown"]++;
    } else {
      (*out)[q->second]++;
    }
  }
}

void ServiceMap::Service::encode(bufferlist& bl, uint64_t features) const
{
  ENCODE_START(1, 1, bl);
  encode(daemons, bl, features);
  encode(summary, bl);
  ENCODE_FINISH(bl);
}

void ServiceMap::Service::decode(bufferlist::const_iterator& p)
{
  DECODE_START(1, p);
  decode(daemons, p);
  decode(summary, p);
  DECODE_FINISH(p);
}

void ServiceMap::Service::dump(Formatter *f) const
{
  f->open_object_section("daemons");
  f->dump_string("summary", summary);
  for (auto& p : daemons) {
    f->dump_object(p.first.c_str(), p.second);
  }
  f->close_section();
}

void ServiceMap::Service::generate_test_instances(std::list<Service*>& ls)
{
  ls.push_back(new Service);
  ls.push_back(new Service);
  ls.back()->daemons["one"].gid = 1;
  ls.back()->daemons["two"].gid = 2;
}

// ServiceMap

void ServiceMap::encode(bufferlist& bl, uint64_t features) const
{
  ENCODE_START(1, 1, bl);
  encode(epoch, bl);
  encode(modified, bl);
  encode(services, bl, features);
  ENCODE_FINISH(bl);
}

void ServiceMap::decode(bufferlist::const_iterator& p)
{
  DECODE_START(1, p);
  decode(epoch, p);
  decode(modified, p);
  decode(services, p);
  DECODE_FINISH(p);
}

void ServiceMap::dump(Formatter *f) const
{
  f->dump_unsigned("epoch", epoch);
  f->dump_stream("modified") << modified;
  f->open_object_section("services");
  for (auto& p : services) {
    f->dump_object(p.first.c_str(), p.second);
  }
  f->close_section();
}

void ServiceMap::generate_test_instances(std::list<ServiceMap*>& ls)
{
  ls.push_back(new ServiceMap);
  ls.push_back(new ServiceMap);
  ls.back()->epoch = 123;
  ls.back()->services["rgw"].daemons["one"].gid = 123;
  ls.back()->services["rgw"].daemons["two"].gid = 344;
  ls.back()->services["iscsi"].daemons["foo"].gid = 3222;
}
