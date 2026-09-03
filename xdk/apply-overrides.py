#!/usr/bin/env python3
"""Apply the source overrides retained in dependency-overrides to deps-src.
Safe to run repeatedly; files are copied byte-for-byte over the pinned checkout.
"""
from pathlib import Path
import argparse, shutil

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='xdk/deps-src directory')
    a=ap.parse_args()
    deps=Path(a.src).resolve()
    root=Path(__file__).resolve().parent.parent
    overrides=root/'dependency-overrides'
    if not overrides.is_dir():
        raise SystemExit('dependency-overrides directory is missing')
    copied=0
    # Override paths are relative to dependency roots named by their top folder.
    roots={
        'opus': deps/'opus',
        'jansson': deps/'jansson',
        'opennow-switch': deps/'opennow-switch',
    }
    for top in overrides.iterdir():
        if not top.is_dir(): continue
        if top.name in roots:
            destroot=roots[top.name]
        else:
            # Existing handoff overrides currently use opus/ and jansson/. Keep
            # this explicit failure so a future override cannot silently land
            # in the wrong dependency tree.
            raise SystemExit('unknown dependency override root: %s' % top.name)
        if not destroot.exists():
            raise SystemExit('dependency is missing for override %s: %s' % (top.name,destroot))
        for src in top.rglob('*'):
            if not src.is_file(): continue
            rel=src.relative_to(top)
            dst=destroot/rel
            dst.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(str(src),str(dst))
            copied += 1
            print('override', dst)

    # libpeer is built as C++ for the XDK's pre-C99 compiler. Keep its IPv4-only
    # configuration free of AF_INET6 references and preserve const correctness.
    agent=deps/'opennow-switch'/'extern'/'libpeer'/'src'/'agent.c'
    text=agent.read_text()
    text=text.replace('for (i = 0; i < 2; i++) {',
                      'for (i = 0; i < sizeof(addr_type) / sizeof(addr_type[0]); i++) {')
    text=text.replace('  switch (addr->family) {\n    case AF_INET6:\n'
                      '      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);\n',
                      '  switch (addr->family) {\n#if CONFIG_IPV6\n    case AF_INET6:\n'
                      '      return udp_socket_sendto(&agent->udp_sockets[1], addr, buf, len);\n#endif\n')
    text=text.replace('  char* pos;\n', '  const char* pos;\n')
    text=text.replace('  snprintf(hostname, pos - urls - 5 + 1, "%s", urls + 5);',
                      '''  {
    size_t hostname_len = (size_t)(pos - (urls + 5));
    if (hostname_len == 0 || hostname_len >= sizeof(hostname)) {
      LOGE("ICE server hostname length invalid: %u", (unsigned)hostname_len);
      return;
    }
    memcpy(hostname, urls + 5, hostname_len);
    hostname[hostname_len] = '\\0';
  }''')
    text=text.replace('IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;\n  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_SRFLX, &bind_addr);',
                      'IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count;\n  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_SRFLX, &bind_addr);\n  agent->local_candidates_count++;')
    text=text.replace('IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count++;\n  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_RELAY, &turn_addr);',
                      'IceCandidate* ice_candidate = agent->local_candidates + agent->local_candidates_count;\n  ice_candidate_create(ice_candidate, agent->local_candidates_count, ICE_CANDIDATE_TYPE_RELAY, &turn_addr);\n  agent->local_candidates_count++;')
    agent.write_text(text)

    mdns=agent.with_name('mdns.c')
    text=mdns.read_text()
    text=text.replace('uint16_t class;', 'uint16_t dns_class;')
    text=text.replace('answer->class', 'answer->dns_class')
    text=text.replace('dns_query->class', 'dns_query->dns_class')
    mdns.write_text(text)

    peer=agent.with_name('peer.c')
    text=peer.read_text().replace('#include <srtp2/srtp.h>', '#include <srtp.h>')
    peer.write_text(text)

    sctp_h=agent.with_name('sctp.h')
    text=sctp_h.read_text().replace('SctpChunkParam param[0];', 'uint8_t params[0];')
    sctp_h.write_text(text)
    sctp=agent.with_name('sctp.c')
    text=sctp.read_text()
    text=text.replace('SctpChunkParam* param = init_ack->param;',
                      'SctpChunkParam* param = (SctpChunkParam*)init_ack->params;')
    text=text.replace('uint8_t* params = (uint8_t*)&init_ack->param[0];',
                      'uint8_t* params = init_ack->params;')
    text=text.replace('dtls_srtp_write(sctp->dtls_srtp, buf, len)',
                      'dtls_srtp_write(sctp->dtls_srtp, (const uint8_t*)buf, len)')
    text=text.replace('strncpy(sctp->stream_table[sctp->stream_count].label, label, sizeof(sctp->stream_table[sctp->stream_count].label));',
                      "strncpy(sctp->stream_table[sctp->stream_count].label, label, sizeof(sctp->stream_table[sctp->stream_count].label) - 1);\n    sctp->stream_table[sctp->stream_count].label[sizeof(sctp->stream_table[sctp->stream_count].label) - 1] = '\\0';")
    text=text.replace('if (length < 12 + label_length + protocol_length)',
                      'if (length < 12 + label_length + protocol_length || label_length >= 32)')
    text=text.replace('char label_str[label_length + 1];', 'char label_str[32];')
    text=text.replace('sctp_outgoing_data(sctp, &ack, 1, DATA_CHANNEL_PPID_CONTROL, sid);',
                      'sctp_outgoing_data(sctp, &ack, 1, (SctpDataPpid)DATA_CHANNEL_PPID_CONTROL, sid);')
    sctp.write_text(text)

    rtp_h=agent.with_name('rtp.h')
    text=rtp_h.read_text().replace('  uint32_t csrc[0];\n\n', '')
    rtp_h.write_text(text)
    pc=agent.with_name('peer_connection.c')
    text=pc.read_text()
    text=text.replace('PeerConnection* pc = calloc(1, sizeof(PeerConnection));',
                      'PeerConnection* pc = (PeerConnection*)calloc(1, sizeof(PeerConnection));')
    text=text.replace('char* msg = calloc(1, msg_size);',
                      'char* msg = (char*)calloc(1, msg_size);')
    text=text.replace('  int packet_processed = 0;\n',
                      '  int packet_processed = 0;\n  int dtls_ret = 0;\n')
    text=text.replace('int dtls_ret = dtls_srtp_handshake',
                      'dtls_ret = dtls_srtp_handshake')
    text=text.replace('''          RtpPacket* rtp = (RtpPacket*)pc->agent_buf;
          const int is_audio_payload = rtp->header.type == PT_PCMU ||
                                       rtp->header.type == PT_PCMA ||
                                       rtp->header.type == PT_OPUS ||
                                       rtp->header.type == 63;''',
                      '''          /* RTP wire fields must not be read through compiler-specific C
             bitfields on big-endian Xenon. */
          const uint8_t payload_type = pc->agent_buf[1] & 0x7f;
          const int is_audio_payload = payload_type == PT_PCMU ||
                                       payload_type == PT_PCMA ||
                                       payload_type == PT_OPUS ||
                                       payload_type == 63;''')
    if 'RTP decrypted packet=' not in text:
        text=text.replace('''                                       payload_type == PT_OPUS ||
                                       payload_type == 63;''',
                          '''                                       payload_type == PT_OPUS ||
                                       payload_type == 63;
          if (pc->completed_rtp_packets <= 12 || pc->completed_rtp_packets % 1200 == 0) {
            LOGI("RTP decrypted packet=%d pt=%u ssrc=%" PRIu32 " bytes=%d expectedAudio=%" PRIu32 " expectedVideo=%" PRIu32 " route=%s",
                 pc->completed_rtp_packets, payload_type, ssrc, pc->agent_ret,
                 pc->remote_assrc, pc->remote_vssrc,
                 (ssrc == pc->remote_assrc || (pc->remote_assrc == 0 && is_audio_payload)) ? "audio" : "video");
          }''')
    text=text.replace('pc->remote_assrc, rtp->header.type);',
                      'pc->remote_assrc, payload_type);')
    text=text.replace('''                      rtp->header.type != PT_PCMU &&
                      rtp->header.type != PT_PCMA &&
                      rtp->header.type != PT_OPUS &&
                      rtp->header.type != 63)) {''',
                      '''                      payload_type != PT_PCMU &&
                      payload_type != PT_PCMA &&
                      payload_type != PT_OPUS &&
                      payload_type != 63)) {''')
    pc.write_text(text)
    rtcp=agent.with_name('rtcp.c')
    text=rtcp.read_text().replace('#ifdef _WIN32\n#include <winsock2.h>',
                                  '#ifdef _XBOX\n#include <winsockx.h>\n#elif defined(_WIN32)\n#include <winsock2.h>')
    rtcp.write_text(text)

    stun=agent.with_name('stun.c')
    text=stun.read_text()
    text=text.replace('  uint16_t* addr16 = (uint16_t*)(&addr->sin6.sin6_addr);',
                      '#if CONFIG_IPV6\n  uint16_t* addr16 = (uint16_t*)(&addr->sin6.sin6_addr);\n#endif')
    text=text.replace('  uint16_t* addr16 = (uint16_t*)&addr->sin6.sin6_addr;',
                      '#if CONFIG_IPV6\n  uint16_t* addr16 = (uint16_t*)&addr->sin6.sin6_addr;\n#endif')
    text=text.replace('    case AF_INET6:\n', '#if CONFIG_IPV6\n    case AF_INET6:\n', 1)
    text=text.replace('    case STUN_FAMILY_IPV6:\n', '#if CONFIG_IPV6\n    case STUN_FAMILY_IPV6:\n', 1)
    text=text.replace('msg->stunclass = ntohs(header->type);', 'msg->stunclass = (StunClass)ntohs(header->type);')
    text=text.replace('msg->stunmethod = ntohs(header->type) & 0x0FFF;', 'msg->stunmethod = (StunMethod)(ntohs(header->type) & 0x0FFF);')
    text=text.replace('      ret = 20;\n      break;\n', '      ret = 20;\n      break;\n#endif\n', 1)
    text=text.replace('        addr16[i] = (*(uint16_t*)(value + 4 + 2 * i) ^ *(uint16_t*)(mask + 2 * i));\n      }\n      break;\n',
                      '        addr16[i] = (*(uint16_t*)(value + 4 + 2 * i) ^ *(uint16_t*)(mask + 2 * i));\n      }\n      break;\n#endif\n', 1)
    stun.write_text(text)

    for header in (agent.with_name('rtp.h'), agent.with_name('rtcp.h')):
        text=header.read_text().replace(
            '#ifdef __BYTE_ORDER\n',
            '#if defined(_XBOX)\n#define __BIG_ENDIAN 4321\n#define __LITTLE_ENDIAN 1234\n#define __BYTE_ORDER __BIG_ENDIAN\n#elif defined(__BYTE_ORDER)\n', 1)
        header.write_text(text)

    socket=agent.with_name('socket.c')
    text=socket.read_text()
    text=text.replace('    case AF_INET6:\n', '#if CONFIG_IPV6\n    case AF_INET6:\n')
    text=text.replace('      case AF_INET6:\n', '#if CONFIG_IPV6\n      case AF_INET6:\n')
    # Each IPv6 case ends immediately before the IPv4 case in this source.
    text=text.replace('      break;\n    case AF_INET:\n', '      break;\n#endif\n    case AF_INET:\n')
    text=text.replace('        break;\n      case AF_INET:\n', '        break;\n#endif\n      case AF_INET:\n')
    text=text.replace('int udp_socket_recvfrom(UdpSocket* udp_socket, Address* addr, uint8_t* buf, int len) {\n  struct sockaddr_in6 sin6;',
                      'int udp_socket_recvfrom(UdpSocket* udp_socket, Address* addr, uint8_t* buf, int len) {\n#if CONFIG_IPV6\n  struct sockaddr_in6 sin6;\n#endif')
    text=text.replace('addr->port = htons(sin.sin_port);', 'addr->port = ntohs(sin.sin_port);')
    text=text.replace('int udp_socket_add_multicast_group(UdpSocket* udp_socket, Address* mcast_addr) {',
                      'int udp_socket_add_multicast_group(UdpSocket* udp_socket, Address* mcast_addr) {\n#ifdef _XBOX\n  (void)udp_socket;\n  (void)mcast_addr;\n  return -1;\n#else')
    marker='\n  return 0;\n}\n\nint udp_socket_open'
    text=text.replace(marker, '\n  return 0;\n#endif\n}\n\nint udp_socket_open', 1)
    text=text.replace('&receive_buffer, sizeof(receive_buffer)', '(const char*)&receive_buffer, sizeof(receive_buffer)')
    text=text.replace('&receive_buffer, &option_size', '(char*)&receive_buffer, &option_size')
    text=text.replace('SO_REUSEADDR, &reuse, sizeof(reuse)', 'SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)')
    text=text.replace('sendto(udp_socket->fd, buf, len', 'sendto(udp_socket->fd, (const char*)buf, len')
    text=text.replace('recvfrom(udp_socket->fd, buf, len', 'recvfrom(udp_socket->fd, (char*)buf, len')
    text=text.replace('send(tcp_socket->fd, buf, len', 'send(tcp_socket->fd, (const char*)buf, len')
    text=text.replace('recv(tcp_socket->fd, buf, len', 'recv(tcp_socket->fd, (char*)buf, len')
    text=text.replace('udp_socket->fd = socket(AF_INET, SOCK_DGRAM, 0);',
                      'udp_socket->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);')
    text=text.replace('udp_socket->fd = socket(AF_INET6, SOCK_DGRAM, 0);',
                      'udp_socket->fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);')
    text=text.replace('tcp_socket->fd = socket(AF_INET, SOCK_STREAM, 0);',
                      'tcp_socket->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);')
    text=text.replace('tcp_socket->fd = socket(AF_INET6, SOCK_STREAM, 0);',
                      'tcp_socket->fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);')
    if 'memset(&udp_socket->bind_addr, 0, sizeof(udp_socket->bind_addr));' not in text:
        text=text.replace('  udp_socket->bind_addr.family = family;',
                          '  memset(&udp_socket->bind_addr, 0, sizeof(udp_socket->bind_addr));\n  udp_socket->bind_addr.family = family;', 1)
    socket_marker='''  if (udp_socket->fd < 0) {
    LOGE("Failed to create socket");
    return -1;
  }
'''
    socket_xnet=socket_marker+'''
#ifdef _XBOX
  {
    BOOL enabled = TRUE;
    int opt5801 = setsockopt(udp_socket->fd, SOL_SOCKET, 0x5801, (const char*)&enabled, sizeof(enabled));
    int err5801 = opt5801 == 0 ? 0 : WSAGetLastError();
    LOGI("XNet UDP direct-outbound option fd=%d opt5801=%d/%d",
         udp_socket->fd, opt5801, err5801);
  }
#endif
'''
    if 'XNet UDP direct-outbound option' not in text:
        text=text.replace(socket_marker,socket_xnet,1)
    socket.write_text(text)

    rtp=agent.with_name('rtp.c')
    text=rtp.read_text()
    text=text.replace('''    if (rtp_decoder->fragment_started) {
      LOGW("RTP H264: sequence gap inside fragmented NALU (%u -> %u)",''',
                      '''    if (rtp_decoder->fragment_started) {
      if (rtp_decoder->sequence_gaps <= 4 || rtp_decoder->sequence_gaps % 100 == 0)
      LOGW("RTP H264: sequence gap inside fragmented NALU (%u -> %u)",''')
    rtp.write_text(text)
    text=rtp.read_text().replace('(FuHeader*)rtp_packet->payload + sizeof(NaluHeader)',
                                 '(FuHeader*)(rtp_packet->payload + sizeof(NaluHeader))')
    text=text.replace('PeerVideoPacket packet = {\n        .data = rtp_decoder->au_buf,\n        .size = rtp_decoder->au_offset,\n        .timestamp = rtp_decoder->au_timestamp,\n        .ssrc = rtp_decoder->au_ssrc,\n      };',
                      'PeerVideoPacket packet;\n      packet.data = rtp_decoder->au_buf;\n      packet.size = rtp_decoder->au_offset;\n      packet.timestamp = rtp_decoder->au_timestamp;\n      packet.ssrc = rtp_decoder->au_ssrc;')
    text=text.replace('PeerAudioPacket packet = {\n      .data = view.payload,\n      .size = view.payload_size,\n      .timestamp = view.timestamp,\n      .ssrc = rtp_get_ssrc(buf),\n      .sequence = read_be16(buf + 2),\n      .payload_type = ((RtpHeader*)buf)->type,\n      .marker = view.marker,\n    };',
                      'PeerAudioPacket packet;\n    packet.data = view.payload;\n    packet.size = view.payload_size;\n    packet.timestamp = view.timestamp;\n    packet.ssrc = rtp_get_ssrc(buf);\n    packet.sequence = read_be16(buf + 2);\n    packet.payload_type = ((RtpHeader*)buf)->type;\n    packet.marker = view.marker;')
    text=text.replace('rtp_decoder->nalu_buf = malloc(', 'rtp_decoder->nalu_buf = (uint8_t*)malloc(')
    text=text.replace('rtp_decoder->au_buf = malloc(', 'rtp_decoder->au_buf = (uint8_t*)malloc(')
    text=text.replace('rtp_decoder->reorder_buf = malloc(', 'rtp_decoder->reorder_buf = (uint8_t*)malloc(')
    text=text.replace('packet.payload_type = ((RtpHeader*)buf)->type;',
                      'packet.payload_type = view.payload_type;')
    rtp.write_text(text)

    # mbedTLS is compiled as C++ because the XDK headers are not C-clean.
    md=deps/'opennow-switch'/'extern'/'libpeer'/'third_party'/'mbedtls'/'library'/'md.c'
    text=md.read_text()
    text=text.replace('mbedtls_sha1_free(ctx->md_ctx);',
                      'mbedtls_sha1_free((mbedtls_sha1_context *) ctx->md_ctx);')
    text=text.replace('mbedtls_sha1_clone(dst->md_ctx, src->md_ctx);',
                      'mbedtls_sha1_clone((mbedtls_sha1_context *) dst->md_ctx,\n                               (const mbedtls_sha1_context *) src->md_ctx);')
    text=text.replace('mbedtls_sha1_starts(ctx->md_ctx);',
                      'mbedtls_sha1_starts((mbedtls_sha1_context *) ctx->md_ctx);')
    text=text.replace('mbedtls_sha1_update(ctx->md_ctx, input, ilen);',
                      'mbedtls_sha1_update((mbedtls_sha1_context *) ctx->md_ctx, input, ilen);')
    text=text.replace('mbedtls_sha1_finish(ctx->md_ctx, output);',
                      'mbedtls_sha1_finish((mbedtls_sha1_context *) ctx->md_ctx, output);')
    md.write_text(text)
    print('Applied %d dependency override file(s).' % copied)
if __name__=='__main__': main()
