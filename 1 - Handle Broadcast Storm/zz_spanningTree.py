# Author : Mrinal Aich

"""
An OpenFlow 1.0 spanning tree implementation.
"""

import logging
import struct

from ryu.base import app_manager
from ryu.controller import mac_to_port
from ryu.controller import ofp_event
from ryu.controller.handler import MAIN_DISPATCHER, CONFIG_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.ofproto import ofproto_v1_0
from ryu.lib.mac import haddr_to_bin
from ryu.lib.packet import packet
from ryu.lib.packet import ethernet
from ryu.lib.packet import ether_types
from ryu.topology.api import get_switch, get_link, get_host
from ryu.app.wsgi import ControllerBase
from ryu.topology import event, switches 
import networkx as nx
from collections import defaultdict
from collections import deque

DEBUG_PATH=0
DEBUG=1

class ProjectController(app_manager.RyuApp):
	
    OFP_VERSIONS = [ofproto_v1_0.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(ProjectController, self).__init__(*args, **kwargs)
        # Graph data structure
        self._graph = {}
        self._graph.setdefault('edges', {})
        self._graph.setdefault('switches', set())
        self._graph.setdefault('hosts', {})
        self.treeLinks = {}

        self.mac_to_port = {}
        self.topology_api_app = self

    # Updates the controller's view of switches in network topology
    def prim_spanningTree(self, src):
        self.treeLinks = {}
        queue = deque()
        color = {}
        WHITE = 0
        BLACK = 1

        for node in self._graph['switches']:
            self.treeLinks[node] = set()
            color[node] = WHITE

        queue.append(src)

        while queue:
            u = queue.popleft()
            color[u] = BLACK
            for v,port in self._graph['edges'][u].items():
                # Improve by using weights among the edges
                # Boils down to simple BFS
                if color[v] == WHITE:
                    self.treeLinks[v].add(u)
                    self.treeLinks[u].add(v)
                    color[v] = BLACK
                    queue.append(v)
        
        if DEBUG:
            print "Spanning Tree: " 
            for u in self._graph['switches']:
                print  str(u) + " <-> " + str(self.treeLinks[u])
        

    def add_flow(self, datapath, in_port, dst, actions):
        ofproto = datapath.ofproto
        
        match = datapath.ofproto_parser.OFPMatch(in_port=in_port, dl_dst=haddr_to_bin(dst))

        mod = datapath.ofproto_parser.OFPFlowMod(datapath=datapath, match=match, cookie=0, command=ofproto.OFPFC_ADD, idle_timeout=0, hard_timeout=0, 
                                                 priority=ofproto.OFP_DEFAULT_PRIORITY, flags=ofproto.OFPFF_SEND_FLOW_REM, actions=actions)
        datapath.send_msg(mod)

    # Packet In Event
    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto

        pkt = packet.Packet(msg.data)
        eth = pkt.get_protocol(ethernet.ethernet)

        if eth.ethertype == ether_types.ETH_TYPE_LLDP: 
            # Ignore lldp packet
            return

        dst = eth.dst
        src = eth.src
        dpid = datapath.id

        if dpid not in self.mac_to_port:
            self.mac_to_port.setdefault(dpid, {})

        #learn a mac address to avoid FLOOD next time.
        if src not in self.mac_to_port[dpid]:
            self.mac_to_port[dpid].setdefault(src, {})

        self.mac_to_port[dpid][src] = msg.in_port
        actions = []

        # Host local to the switch
        if dst in self.mac_to_port[dpid]:
            out_port = self.mac_to_port[dpid][dst]
            actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))

            # install a flow to avoid packet_in next time
            self.add_flow(datapath, msg.in_port, dst, actions)
    
            data = None
            if msg.buffer_id == ofproto.OFP_NO_BUFFER:
                data = msg.data
            out = datapath.ofproto_parser.OFPPacketOut( datapath=datapath, buffer_id=msg.buffer_id, in_port=msg.in_port, actions=actions, data=data)
            datapath.send_msg(out)
            print "Packet: dpid - " + str(dpid) + " || " + str(eth.src) + " -> " + str(eth.dst) + " | Ingress Port: " + str(msg.in_port)

        # Flood along the edges of the spanning Tree
        else:
            flag = 0
            # Forwarding to the switches
            if dpid in self._graph['edges']:
                for key,value in self._graph['edges'][dpid].items():
                    if key in self.treeLinks and dpid in self.treeLinks[key]:
                        if DEBUG_PATH:
                            print str(dpid) + " | Switch: " + str(key) + " <-> Port : " + str(value)
                        out_port = value
                        actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))
                        flag = 1

            # Forwarding to the hosts
            if dpid in self._graph['hosts']:
                for key,value in self._graph['hosts'][dpid].items():
                    if value != msg.in_port:
                        if DEBUG_PATH:
                            print str(dpid) + " | Host: " + str(key) + " <-> Port : " + str(value)
                        out_port = value
                        actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))
                        flag = 1

            if flag:
                data = None
                if msg.buffer_id == ofproto.OFP_NO_BUFFER:
                    data = msg.data
                out = datapath.ofproto_parser.OFPPacketOut( datapath=datapath, buffer_id=msg.buffer_id, in_port=msg.in_port, actions=actions, data=data)
                datapath.send_msg(out)

    # Host Add Event
    @set_ev_cls(event.EventHostAdd, MAIN_DISPATCHER)
    def update_topology_hostAdd(self, ev):
        mac = ev.host.mac
        dpid = ev.host.port.dpid

        # Switch's host information
        host_list = get_host(self.topology_api_app,dpid)
        for item in host_list:
            mac = item.mac
            dpid = item.port.dpid
            port = item.port.port_no

            if port != 1:
                return
            
            if mac not in self._graph['hosts'][dpid]:
                self._graph['hosts'][dpid].setdefault(mac,{})

            self._graph['hosts'][dpid][mac]=port

            if DEBUG:
                print "Saved Host information : " + str(mac) + " : " + str(dpid)

        # Update network's Spanning Tree
        print "Event: Host Added - Re-calcuting Spanning Tree"
        for item in self._graph['switches']:
            self.prim_spanningTree(item)
            break

    # Link add event
    @set_ev_cls(event.EventLinkAdd)
    def update_topology_linkAdd(self, ev):
        link = ev.link
        # Switches inter-connections
        self._graph['edges'][link.src.dpid][link.dst.dpid] = link.src.port_no
        self._graph['edges'][link.dst.dpid][link.src.dpid] = link.dst.port_no

        if DEBUG:
            print "Saved Link Information: " + str(link.src.dpid) + " <-> " + str(link.dst.dpid)


    # Switch Enter Event
    @set_ev_cls(event.EventSwitchEnter)
    def update_topology_switchEnter(self, ev):
        switch_list = get_switch(self.topology_api_app, None)
        switches=[switch.dp.id for switch in switch_list]

        # Add Switch
        for switch in switch_list:
            dpid= switch.dp.id
            self._graph['switches'].add(dpid)
            
            if dpid not in self._graph['edges']:
                self._graph['edges'].setdefault(dpid, {})
            if dpid not in self._graph['hosts']:
                self._graph['hosts'].setdefault(dpid,{})

    # Switch Leave event
    @set_ev_cls(event.EventSwitchLeave)
    def update_topology_switchLeave(self, ev):
        print "Event: Switch Deleted - Re-calculating Spanning Tree"
        switch_list = get_switch(self.topology_api_app, None)
        switches=[switch.dp.id for switch in switch_list]

        # TODO: Improve the logic
        for mem_switch in self._graph['switches']:
            flag = 0
            for switch in switch_list:
                if switch.dp.id == mem_switch:
                    flag = 1
            
            if flag == 0:
                dpid = mem_switch
                break

        # Remove Switch entries
        self._graph['switches'].remove(dpid)
        for other_dpid,linked_dpid in self._graph['edges'].items():
            for link,port in linked_dpid.items():
                if link == dpid:
                    self._graph['edges'][other_dpid].pop(link)
        
        if dpid in self._graph['edges']:
            self._graph['edges'].pop(dpid,None)
            print self._graph['edges']
        
        if dpid in self._graph['hosts']:
            self._graph['hosts'].pop(dpid,None)
        if dpid in self.mac_to_port:
            self.mac_to_port.pop(dpid,None)

        # Update network's Spanning Tree
        for item in self._graph['switches']:
            self.prim_spanningTree(item)
            break
