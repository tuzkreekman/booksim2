// $Id$

/*
 Copyright (c) 2007-2011, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this 
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <cstdlib>

#include "workload.hpp"
#include "random_utils.hpp"

Workload::Workload(int nodes) : _nodes(nodes)
{
  
}

Workload::~Workload()
{
  
}

Workload * Workload::New(string const & workload, int nodes,
			 Configuration const * const config)
{
  string workload_name;
  string param_str;
  size_t left = workload.find_first_of('(');
  if(left == string::npos) {
    cout << "Error: Missing parameter in workload specification: " << workload
	 << endl;
    exit(-1);
  }
  workload_name = workload.substr(0, left);
  size_t right = workload.find_last_of(')');
  if(right == string::npos) {
    param_str = workload.substr(left+1);
  } else {
    param_str = workload.substr(left+1, right-left-1);
  }
  vector<string> params = tokenize_str(param_str);
  
  Workload * result = NULL;
  if(workload_name == "null") {
    result = new NullWorkload(nodes);
  } else if(workload_name == "synthetic") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in synthetic workload definition: "
	   << workload << endl;
      exit(-1);
    }
    double load = atof(params[0].c_str());
    string traffic = params[1];
    string injection = (params.size() > 2) ? params[2] : (config ? config->GetStr("injection_process") : "bernoulli");
    vector<int> sizes = (params.size() > 3) ? tokenize_int(params[3]) : vector<int>(1, config ? config->GetInt("packet_size") : 1);
    vector<int> rates = (params.size() > 4) ? tokenize_int(params[4]) : vector<int>(1, 1);
    rates.resize(sizes.size(), rates.back());
    result = new SyntheticWorkload(nodes, load, traffic, injection, sizes, 
				   rates, config);
  } else if(workload_name == "trace") {
    if(params.size() < 2) {
      cout << "Error: Missing parameter in trace workload definition: "
	   << workload << endl;
      exit(-1);
    }
    vector<string> filenames = tokenize_str(params[0]);
    filenames.resize(nodes, filenames.back());
    vector<int> packet_sizes = tokenize_int(params[1]);
    vector<int> limits;
    vector<int> scales;
    vector<int> skips;
    if(params.size() > 2) {
      limits = tokenize_int(params[2]);
      if(params.size() > 3) {
	skips = tokenize_int(params[3]);
	skips.resize(nodes, skips.back());
	if(params.size() > 4) {
	  scales = tokenize_int(params[4]);
	  scales.resize(nodes, scales.back());
	}
      }
    }
    result = new TraceWorkload(nodes, filenames, packet_sizes, limits, skips, scales);
  }
  return result;
}

void Workload::reset()
{
  _time = 0;
}

void Workload::advanceTime()
{
  ++_time;
}

SyntheticWorkload::SyntheticWorkload(int nodes, double load, 
				     string const & traffic, 
				     string const & injection, 
				     vector<int> const & sizes, 
				     vector<int> const & rates, 
				     Configuration const * const config)
  : Workload(nodes), _sizes(sizes), _rates(rates), _max_val(-1)
{
  _injection = InjectionProcess::New(injection, nodes, load, config);
  _traffic = TrafficPattern::New(traffic, nodes, config);
  _qtime.resize(nodes);
  int size = rates.size();
  for(int i = 0; i < size; ++i) {
    int rate = rates[i];
    assert(rate >= 0);
    _max_val += rate;
  }
}

SyntheticWorkload::~SyntheticWorkload()
{
  delete _injection;
  delete _traffic;
}

void SyntheticWorkload::reset()
{
  Workload::reset();
  _qtime.assign(_nodes, 0);
  _injection->reset();
  _traffic->reset();
  while(!_ready.empty()) {
    _ready.pop();
  }
  while(!_pending.empty()) {
    _pending.pop();
  }
  while(!_deferred.empty()) {
    _deferred.pop();
  }
  for(int source = 0; source < _nodes; ++source) {
    if(_injection->test(source)) {
      _pending.push(source);
    } else {
      _ready.push(source);
    }
  }
}

void SyntheticWorkload::advanceTime()
{
  Workload::advanceTime();
  while(!_deferred.empty()) {
    int const & source = _deferred.front();
    _pending.push(source);
    _deferred.pop();
  }
  for(size_t i = 0; i < _ready.size(); ++i) {
    int const & source = _ready.front();
    bool generated = false;
    while(_qtime[source] < _time) {
      ++_qtime[source];
      if(_injection->test(source)) {
	generated = true;
	break;
      }
    }
    if(generated) {
      _pending.push(source);
    } else {
      _ready.push(source);
    }
    _ready.pop();
  }
}

bool SyntheticWorkload::empty() const
{
  return _pending.empty();
}

bool SyntheticWorkload::completed() const
{
  return false;
}

int SyntheticWorkload::source() const
{
  assert(!empty());
  int const source = _pending.front();
  assert((source >= 0) && (source < _nodes));
  return source;
}

int SyntheticWorkload::dest() const
{
  assert(!empty());
  int const dest = _traffic->dest(source());
  assert((dest >= 0) && (dest < _nodes));
  return dest;
}

int SyntheticWorkload::size() const
{
  assert(!empty());
  int num_sizes = _sizes.size();
  if(num_sizes == 1) {
    return _sizes[0];
  }
  int pct = RandomInt(_max_val);
  for(int i = 0; i < (num_sizes - 1); ++i) {
    int const limit = _rates[i];
    if(limit > pct) {
      return _sizes[i];
    } else {
      pct -= limit;
    }
  }
  assert(_rates.back() > pct);
  return _sizes.back();
}

int SyntheticWorkload::time() const
{
  assert(!empty());
  int const & source = _pending.front();
  return _qtime[source];
}

void SyntheticWorkload::inject(int pid)
{
  assert(!empty());
  int const & source = _pending.front();
  _ready.push(source);
  _pending.pop();
}

void SyntheticWorkload::defer()
{
  assert(!empty());
  int const & source = _pending.front();
  _deferred.push(source);
  _pending.pop();
}

TraceWorkload::TraceWorkload(int nodes, vector<string> const & filenames, 
			     vector<int> const & packet_sizes, 
			     vector<int> const & limits,
			     vector<int> const & skips, 
			     vector<int> const & scales)
  : Workload(nodes), _packet_sizes(packet_sizes), _limits(limits), 
    _scales(scales), _skips(skips)
{
  if(_limits.empty()) {
    _limits.push_back(-1);
  }
  _limits.resize(nodes, _limits.back());

  if(_skips.empty()) {
    _skips.push_back(0);
  }
  _skips.resize(nodes, _skips.back());

  if(_scales.empty()) {
    _scales.push_back(1);
  }
  _scales.resize(nodes, _scales.back());

  _traces.resize(nodes);
  for(int n = 0; n < _nodes; ++n) {
    string const & filename = (n < filenames.size()) ? filenames[n] : filenames.back();
    ifstream * trace = new ifstream(filename.c_str());
    _traces[n] = trace;
    if(!trace->is_open()) {
      cerr << "Unable to open trace file: " << filename << endl;
      exit(-1);
    }
  }
}

TraceWorkload::~TraceWorkload()
{
  for(int n = 0; n < _nodes; ++n) {
    ifstream * const trace = _traces[n];
    if(trace) {
      if(trace->is_open()) {
	trace->close();
      }
      delete trace;
    }
  }
}

void TraceWorkload::reset()
{
  Workload::reset();

  _counts.assign(_nodes, 0);

  // get first packet for each node
  for(int n = 0; n < _nodes; ++n) {
    ifstream * trace = _traces[n];
    trace->seekg(0);
    int & count = _counts[n];
    int const limit = _limits[n];
    int const skip = _skips[n];
    int const scale = _scales[n];
    int time = 0;
    while(((limit < 0) || (count < limit)) && !trace->eof()) {
      ++count;
      int delay, source, dest, type;
      *trace >> delay >> source >> dest >> type;
      if(count > skip) {
	time += delay;
	if((source == n) && (type >= 0)) {
	  PacketInfo pi;
	  pi.time = time;
	  pi.source = source;
	  pi.dest = dest;
	  pi.size = _packet_sizes[type];
	  assert(pi.size > 0);
	  if(((scale > 0) ? (time / scale) : (time * -scale)) > 0) {
	    _waiting_packets.push_back(pi);
	  } else {
	    _ready_packets.push_back(pi);
	  }
	  break;
	}
      }
    }
  }
  _ready_iter = _ready_packets.begin();
}

void TraceWorkload::advanceTime()
{
  Workload::advanceTime();

  // promote from waiting to ready
  list<PacketInfo>::iterator iter = _waiting_packets.begin();
  while(iter != _waiting_packets.end()) {
    int time = iter->time;
    int scale = _scales[iter->source];
    if(((scale > 0) ? (time / scale) : (time * -scale)) <= _time) {
      list<PacketInfo>::iterator source_iter = iter;
      ++iter;
      _ready_packets.splice(_ready_packets.end(), _waiting_packets, source_iter);
    } else {
      ++iter;
    }
  }

  assert(_ready_iter == _ready_packets.end());
  _ready_iter = _ready_packets.begin();
}

bool TraceWorkload::empty() const
{
  return (_ready_iter == _ready_packets.end());
}

bool TraceWorkload::completed() const
{
  return (_waiting_packets.empty() && _ready_packets.empty());
}

int TraceWorkload::source() const
{
  assert(!empty());
  int const source = _ready_iter->source;
  assert((source >= 0) && (source < _nodes));
  return source;
}

int TraceWorkload::dest() const
{
  assert(!empty());
  int const dest = _ready_iter->dest;
  assert((dest >= 0) && (dest < _nodes));
  return dest;
}

int TraceWorkload::size() const
{
  assert(!empty());
  assert(_ready_iter->size > 0);
  return _ready_iter->size;
}

int TraceWorkload::time() const
{
  assert(!empty());
  int time = _ready_iter->time;
  int scale = _scales[_ready_iter->source];
  return (scale > 0) ? (time / scale) : (time * -scale);
}

void TraceWorkload::inject(int pid)
{
  assert(!empty());
  int const n = _ready_iter->source;
  int & count = _counts[n];
  int const limit = _limits[n];
  int const scale = _scales[n];
  ifstream * trace = _traces[n];
  int time = _ready_iter->time;
  bool empty = true;
  while(((limit < 0) || (count < limit)) && !trace->eof()) {
    ++count;
    int delay, source, dest, type;
    *trace >> delay >> source >> dest >> type;
    time += delay;
    if((source == n) && (type >= 0)) {
      _ready_iter->time = time;
      assert(_ready_iter->source == source);
      _ready_iter->dest = dest;
      _ready_iter->size = _packet_sizes[type];
      assert(_ready_iter->size > 0);
      empty = false;
      break;
    }
  }
  if(empty) {
    _ready_iter = _ready_packets.erase(_ready_iter);
  } else if(((scale > 0) ? (time / scale) : (time * -scale)) > _time) {
    list<PacketInfo>::iterator source_iter = _ready_iter;
    ++_ready_iter;
    _waiting_packets.splice(_waiting_packets.end(), _ready_packets, source_iter);
  } else {
    ++_ready_iter;
  }
}

void TraceWorkload::defer()
{
  assert(!empty());
  ++_ready_iter;
}
