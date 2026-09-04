#!/usr/bin/env python3
from pathlib import Path
import argparse

def replace(path, old, new):
    p=Path(path); s=p.read_text()
    if old in s:
        p.write_text(s.replace(old,new))
        print('patched',p)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--src', required=True); a=ap.parse_args()
    root=Path(a.src).resolve(); peer=root/'opennow-switch'/'extern'/'libpeer'/'src'
    if not peer.exists(): raise SystemExit('libpeer not found')
    replace(peer/'ports.c', '#elif defined(__SWITCH__)', '#elif defined(__SWITCH__) || defined(_XBOX)')
    # Diagnostics are normally disabled, but make any explicitly enabled traces land beside the XEX.
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/signaling.log', 'game:\\\\signaling.log')
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/stream_trace.log', 'game:\\\\stream_trace.log')
    replace(peer/'sctp.c', 'sdmc:/switch/OpenNOWSwitch/input.log', 'game:\\\\input.log')

    # The Xbox 360 XDK C compiler is pre-C99 and the port is IPv4-only.
    agent = peer/'agent.c'
    replace(agent,
'''  switch (addr->family) {
    case AF_INET6:
      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);
    case AF_INET:
''',
'''  switch (addr->family) {
#if CONFIG_IPV6
    case AF_INET6:
      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);
#endif
    case AF_INET:
''')
    # Make both bundled and freshly-fetched libpeer trees C89-safe.  Do the
    # assignment rewrite as well as the declaration insertion so this remains
    # idempotent when a prior commit already split candidate-count increments.
    replace(agent,
'''  StunMessage recv_msg;
  memset(&send_msg, 0, sizeof(send_msg));
''',
'''  StunMessage recv_msg;
  IceCandidate* ice_candidate;
  memset(&send_msg, 0, sizeof(send_msg));
''')
    replace(agent,
'''  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count;
''',
'''  ice_candidate = agent->local_candidates + agent->local_candidates_count;
''')
    replace(agent,
'''  memcpy(&bind_addr, &recv_msg.mapped_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
''',
'''  memcpy(&bind_addr, &recv_msg.mapped_addr, sizeof(Address));
  ice_candidate = agent->local_candidates + agent->local_candidates_count++;
''')
    replace(agent,
'''  Address turn_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
''',
'''  Address turn_addr;
  StunMessage send_msg;
  StunMessage recv_msg;
  IceCandidate* ice_candidate;
''')
    replace(agent,
'''  memcpy(&turn_addr, &recv_msg.relayed_addr, sizeof(Address));
  IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;
''',
'''  memcpy(&turn_addr, &recv_msg.relayed_addr, sizeof(Address));
  ice_candidate = agent->local_candidates + agent->local_candidates_count++;
''')
    replace(agent,
'''void agent_get_local_description(Agent* agent, char* description, int length) {
  for (int i = 0; i < agent->local_candidates_count; i++) {
''',
'''void agent_get_local_description(Agent* agent, char* description, int length) {
  int i;
  for (i = 0; i < agent->local_candidates_count; i++) {
''')
    replace(agent,
'''static void agent_create_binding_request(Agent* agent, StunMessage* msg) {
  uint64_t tie_breaker = 0;  // always be controlled
  // send binding request
  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);
  char username[584];
''',
'''static void agent_create_binding_request(Agent* agent, StunMessage* msg) {
  uint64_t tie_breaker = 0;  // always be controlled
  char username[584];
  // send binding request
  stun_msg_create(msg, STUN_CLASS_REQUEST | STUN_METHOD_BINDING);
''')
    replace(agent,
'''  int i;

  LOGD("Set remote description:\\n%s", description);

  char* line_start = description;
  char* line_end = NULL;
''',
'''  int i;
  char* line_start = description;
  char* line_end = NULL;

  LOGD("Set remote description:\\n%s", description);
''')
    replace(agent,
'''void agent_get_candidate_pair_stats(Agent* agent, AgentCandidatePairStats* stats) {
  if (!agent || !stats) {
''',
'''void agent_get_candidate_pair_stats(Agent* agent, AgentCandidatePairStats* stats) {
  int i;
  if (!agent || !stats) {
''')
    replace(agent,
'''  for (int i = 0; i < agent->candidate_pairs_num; i++) {
''',
'''  for (i = 0; i < agent->candidate_pairs_num; i++) {
''')

    # FFmpeg 1.2's slice worker pool already has a startup barrier: the caller
    # holds current_job_lock while creating workers, then cond_wait releases it
    # so each worker can enter and park.  The Xbox fork added an unsafe extra
    # step that suspends a just-created pthread, extracts its native handle,
    # changes processor affinity, and resumes it before reaching that barrier.
    # On Xenon this can leave a worker suspended/not runnable forever, which is
    # exactly the avcodec_open2() hang seen when thread_count > 1.  Restore the
    # upstream slice-worker startup semantics and let the XDK scheduler place
    # those workers.  Frame-thread affinity code is intentionally untouched.
    xb_thread = root/'xbmc360'/'libraries'/'ffmpeg'/'libavcodec'/'xb_thread.c'
    if not xb_thread.exists():
        raise SystemExit('Xbox FFmpeg thread backend not found: %s' % xb_thread)
    replace(xb_thread,
'''        /* Xbox 360: Pin worker threads to specific hardware threads */
        hThread = pthread_getw32threadhandle_np(c->workers[i]);
        SuspendThread(hThread);
        XSetThreadProcessor(hThread, hw_thread(i));
        ResumeThread(hThread);
''',
'''        /* Keep FFmpeg 1.2's native pthread startup/barrier semantics here.
           Do not suspend or retarget a pthread before it has entered worker(). */
''')
    xb_text = xb_thread.read_text()
    if 'pthread_getw32threadhandle_np(c->workers[i])' in xb_text:
        raise SystemExit('unsafe Xbox FFmpeg slice-worker affinity block is still present')

if __name__=='__main__': main()
