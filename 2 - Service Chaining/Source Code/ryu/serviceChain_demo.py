# Author : Mrinal Aich

'''
Features:
    1. Dynamic Flow rule addition

Limitations :
    1. Assuming all the Service chain middleboxes lies within the flowpath of the packets
'''

from ryu.base import app_manager
from ryu.controller import mac_to_port
from ryu.controller import ofp_event
from ryu.controller.handler import CONFIG_DISPATCHER, MAIN_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.ofproto import ofproto_v1_3
from ryu.lib.packet import packet
from ryu.lib.packet import ethernet
from ryu.lib.packet import ether_types
from ryu.topology.api import get_switch, get_link, get_host
from ryu.topology import event, switches
from collections import defaultdict
from collections import deque
from sets import Set

LOG_DEBUG=0
LOG_INFO=1

HOST1_MAC_ADDR = "00:00:00:00:00:01"
HOST2_MAC_ADDR = "00:00:00:00:00:02"
HOST3_MAC_ADDR = "00:00:00:00:00:03"

MB1_MAC_ADDR   = "00:00:00:00:00:21"
MB2_MAC_ADDR   = "00:00:00:00:00:22"
MB3_MAC_ADDR   = "00:00:00:00:00:23"

SWITCH_1_DPID = 1
SWITCH_2_DPID = 2
SWITCH_3_DPID = 3

VLAN_ID_RCVD_FROM_ANY_HOST  = 0
VLAN_ID_MIDDLEBOX_MB1       = 1
VLAN_ID_MIDDLEBOX_MB2       = 2
VLAN_ID_MIDDLEBOX_MB3       = 3
VLAN_ID_S1_S3_TUNNEL_VIA_S2 = 4
VLAN_ID_POLICY_COMPLETE     = 5
ID_NOT_TO_BE_USED           = 6
VLAN_ID_APPLY_SERVICE_CHAIN = 7


MIDDLEBOX_SWITCHES  = Set([SWITCH_1_DPID, SWITCH_3_DPID])
TUNNELLED_SWITCHES  = Set([SWITCH_2_DPID])
KNOWN_HOST_MAC_ADDR = Set([HOST1_MAC_ADDR, HOST2_MAC_ADDR, HOST3_MAC_ADDR])

SUCCESS = 1
FAILURE = 0

COLOR_WHITE = 0
COLOR_BLACK = 1

class make_tuple():

    def __init__(self, value1, value2, value3=None):
        self.val1 = value1
        self.val2 = value2
        self.val3 = value3

class SimpleSwitch13(app_manager.RyuApp):
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    def __init__(self, *args, **kwargs):
        super(SimpleSwitch13, self).__init__(*args, **kwargs)
        # Graph data structure
        self.m_graph = {}
        self.m_graph.setdefault('edges', {})
        self.m_graph.setdefault('switches', set())
        self.m_graph.setdefault('hosts', {})
        self.m_treeLinks = {}
        self.m_mac_to_dpid = {}

        self.m_mac_to_port = {}
        self.m_topology_api_app = self

        # Data Structure for Forwarding-Port and VLAN-Id
        self.m_dpid = {}
        self.m_dpid.setdefault(SWITCH_1_DPID, {})
        self.m_dpid.setdefault(SWITCH_2_DPID, {})
        self.m_dpid.setdefault(SWITCH_3_DPID, {})

        # Mandatory Flows for the Service Chaining - SW1 -> MB1 -> SW1 -> SW2 -> SW3 -> MB3 -> SW3 -> SW2 -> SW1 -> MB2 -> SW1
        # Switch-S1
        self.m_dpid[SWITCH_1_DPID][make_tuple(4,VLAN_ID_MIDDLEBOX_MB1)]                 = make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)
        self.m_dpid[SWITCH_1_DPID][make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)]           = make_tuple(5,VLAN_ID_MIDDLEBOX_MB3)

        # Switch-S2
        # Note : No inserting/modification of VLAN-tag by this (Tunnelled) switch
        self.m_dpid[SWITCH_2_DPID][make_tuple(1,VLAN_ID_S1_S3_TUNNEL_VIA_S2)]           = make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)
        self.m_dpid[SWITCH_2_DPID][make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)]           = make_tuple(1,VLAN_ID_S1_S3_TUNNEL_VIA_S2)

        # Switch-S3
        self.m_dpid[SWITCH_3_DPID][make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)]           = make_tuple(3,VLAN_ID_MIDDLEBOX_MB2)
        self.m_dpid[SWITCH_3_DPID][make_tuple(4,VLAN_ID_MIDDLEBOX_MB2)]                 = make_tuple(2,VLAN_ID_S1_S3_TUNNEL_VIA_S2)

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev):
        datapath = ev.msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        dpid = datapath.id

        # Add Mandatory flow Entries w.r.t. the Service Chain
        if dpid in self.m_dpid:
            for key,value in self.m_dpid[dpid].items():
                
                # Switches connected to a MiddleBox
                if dpid not in TUNNELLED_SWITCHES:
                    match = parser.OFPMatch(in_port = key.val1, vlan_vid=(0x1000 | key.val2))
                    actions = [parser.OFPActionSetField(vlan_vid=(0x1000 | value.val2)),
                               parser.OFPActionOutput(value.val1)]
                
                # Tunnelled Switches
                else:
                    match = parser.OFPMatch(in_port = key.val1)
                    actions = [parser.OFPActionOutput(value.val1)]

                self.add_flow(datapath, 0, match, actions)

    def add_flow(self, datapath, priority, match, actions, buffer_id=None):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        inst = [parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS, actions)]
        if buffer_id:
            mod = parser.OFPFlowMod(datapath=datapath, buffer_id=buffer_id,
                                    priority=priority, match=match, instructions=inst)
        else:
            mod = parser.OFPFlowMod(datapath=datapath, priority=priority,
                                    match=match, instructions=inst)
        datapath.send_msg(mod)


    def getDatapath(self, dpid):
        datapath = None
        # Retreive datapath of 'this_switch'
        for dp in self.m_graph['switches']:
            if dp.id == dpid:
                datapath = dp
                break

        return datapath


    def install_flow_rules(self, parser, flowpath, host):
       
        dpid = self.m_mac_to_dpid[host]
        dct = dict([(dpid, host)])

        if LOG_INFO:
            print "Installing Flows for : " + str(dct)

        # Actor:Ingress/Egress switches
        # Rule 1.
        # Rule 1a.
            # Add flow rule for destination_mac 
            # Takes into account host is connected to its switch including 'dpid1'
        in_port = self.m_mac_to_port[dpid][host]
        datapath = self.getDatapath(dpid)

        vlan_id = VLAN_ID_MIDDLEBOX_MB3 if dpid == SWITCH_1_DPID else VLAN_ID_POLICY_COMPLETE
        match = parser.OFPMatch(eth_dst=host, vlan_vid=(0x1000 | vlan_id))
        actions = [parser.OFPActionPopVlan(0x8100),
                   parser.OFPActionOutput(in_port)]
        self.add_flow(datapath, 0, match, actions)
        
        # Rule 1b.
        if dpid != SWITCH_1_DPID:
            match = parser.OFPMatch(eth_src=host, in_port=in_port)
            next_switch = flowpath[-2] if dpid == flowpath[-1] else flowpath[1]
            out_port = self.m_graph['edges'][dpid][next_switch]
            actions = [parser.OFPActionPushVlan(0x8100),
                       parser.OFPActionSetField(vlan_vid=(0x1000 | VLAN_ID_APPLY_SERVICE_CHAIN)),
                       parser.OFPActionOutput(out_port)]
            self.add_flow(datapath, 0, match, actions)
        # else: Handled later


        # Actor: Intermediate Switches 
        # Rule 2.
        if len(flowpath) > 2:
            # Intermediate Switches to forward to the next Switch till it reaches 
            # either the Destination switch or MiddleBox1-switch
            # Adding corresponding flow rules

            temp_flowpath = flowpath
            if dpid == flowpath[-1]:
                temp_flowpath.reverse()

            if temp_flowpath[0] != SWITCH_1_DPID:
                for i in range(1,len(temp_flowpath)-1):

                    # All the intermediate switch on the way to MIDDLEBOX-Switch1
                    # should be configured
                    if temp_flowpath[i] == SWITCH_1_DPID:
                        break

                    this_switch = temp_flowpath[i]

                    datapath = self.getDatapath(this_switch)

                    # Rule 2a. Flow towards the MiddleBox1-switch
                    match = parser.OFPMatch(eth_src=host, vlan_vid=(0x1000 | VLAN_ID_APPLY_SERVICE_CHAIN))
                    next_switch = temp_flowpath[i+1]
                    out_port = self.m_graph['edges'][this_switch][next_switch]
                    actions = [parser.OFPActionOutput(out_port)]
                    self.add_flow(datapath, 0, match, actions)

                    # Rule 2b. Flow towards destination switch
                    match = parser.OFPMatch(eth_dst=host, vlan_vid=(0x1000 | VLAN_ID_POLICY_COMPLETE))
                    previous_switch = temp_flowpath[i-1]
                    out_port = self.m_graph['edges'][this_switch][previous_switch]
                    actions = [parser.OFPActionOutput(out_port)]
                    self.add_flow(datapath, 0, match, actions)

        # Actor: MiddleBox-Switch1
        # Rule 3 : Receiving : Service chain starts
        datapath = self.getDatapath(SWITCH_1_DPID)
        # I. Add flow rule in MiddleBox-Switch1 on vlan_id = VLAN_ID_APPLY_SERVICE_CHAIN
        if dpid != SWITCH_1_DPID:
            match = parser.OFPMatch(eth_src=host, vlan_vid=(0x1000 | VLAN_ID_APPLY_SERVICE_CHAIN))
            actions = [parser.OFPActionSetField(vlan_vid=(0x1000 | VLAN_ID_MIDDLEBOX_MB1)),
                       parser.OFPActionOutput(3)]
            self.add_flow(datapath, 0, match, actions)

        # II. Host connected to the MiddleBox-Switch1
        else:
            match = parser.OFPMatch(eth_src=host, in_port=self.m_mac_to_port[dpid][host])
            actions = [parser.OFPActionPushVlan(0x8100),
                       parser.OFPActionSetField(vlan_vid=(0x1000 | VLAN_ID_MIDDLEBOX_MB1)),
                       parser.OFPActionOutput(3)]
            self.add_flow(datapath, 0, match, actions)
        

        # Rule 4 : Returning : Service chain ends
        # Send to destination by MiddleBox-Switch1 on vlan_id = VLAN_ID_MIDDLEBOX_MB3
        # Packet has to delivered to the destination
        temp_flowpath = flowpath
        if dpid == temp_flowpath[0]:
            temp_flowpath.reverse()

        # Host not connected to the MiddleBox-Switch1
        if temp_flowpath[-1] != SWITCH_1_DPID:
            # Suppose, 4->1->2->3, install flow to send to 2.
            next_switch = None
            for i in range(len(temp_flowpath)):
                if temp_flowpath[i] == SWITCH_1_DPID:
                    next_switch = temp_flowpath[i+1]
                    break

            if next_switch == None:
                print "Error:"
                print "Flowpath : " + str(temp_flowpath)
                print "hosts: " + str(dct)
                return

            match = parser.OFPMatch(eth_dst=host, vlan_vid=(0x1000 | VLAN_ID_MIDDLEBOX_MB3))
            out_port = self.m_graph['edges'][SWITCH_1_DPID][next_switch]
            actions = [parser.OFPActionSetField(vlan_vid=(0x1000 | VLAN_ID_POLICY_COMPLETE)),
                       parser.OFPActionOutput(out_port)]
            self.add_flow(datapath, 0, match, actions)


    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        # If you hit this you might want to increase
        # the "miss_send_length" of your switch
        if ev.msg.msg_len < ev.msg.total_len:
            self.logger.debug("packet truncated: only %s of %s bytes",
                              ev.msg.msg_len, ev.msg.total_len)
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        in_port = msg.match['in_port']

        pkt = packet.Packet(msg.data)
        eth = pkt.get_protocols(ethernet.ethernet)[0]

        if eth.ethertype == ether_types.ETH_TYPE_LLDP:
            # ignore lldp packet
            return
        dst = eth.dst
        src = eth.src
        dpid = datapath.id

        if dpid not in self.m_mac_to_port:
            self.m_mac_to_port.setdefault(dpid, {})


        # learn a mac address to avoid FLOOD next time.
        self.m_mac_to_port[dpid][src] = in_port
        actions = []

        # Since, the 'src' and 'dst' MAC with their dpid's, path can be found out
        # and rules can be installed.
        if dst in self.m_mac_to_port[dpid]:

            match = []
            actions = []      

            if self.m_mac_to_dpid[dst] == dpid:
                # Install Service-chain flows to avoid packet_in next time
                flowpath = self.get_flow_path(src, dst)

                # Add flow rules along the path
                # First and Last elements are the ingree and egress switch's dpid
                self.install_flow_rules(parser, flowpath, src)
                self.install_flow_rules(parser, flowpath, dst)

            # Send the immediate Action by the switch for this packet only
            match = parser.OFPMatch(in_port=in_port, eth_dst=dst)
            out_port = self.m_mac_to_port[dpid][dst]
            actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))
            data = None
            if msg.buffer_id == ofproto.OFP_NO_BUFFER:
                data = msg.data
            out = datapath.ofproto_parser.OFPPacketOut( datapath=datapath, buffer_id=msg.buffer_id, in_port=in_port, actions=actions, data=data)
            datapath.send_msg(out)
            if LOG_DEBUG:
                print "Packet: dpid - " + str(dpid) + " || " + str(eth.src) + " -> " + str(eth.dst) + " | Ingress Port: " + str(in_port)

        # Flood along the edges of the spanning Tree
        else:
            flag = 0
            match = []
            actions = []

            # Forwarding to the switches
            if dpid in self.m_graph['edges']:
                for key,value in self.m_graph['edges'][dpid].items():
                    if key in self.m_treeLinks and dpid in self.m_treeLinks[key]:
                        if LOG_DEBUG:
                            print str(dpid) + " | Switch: " + str(key) + " <-> Port : " + str(value)
                        out_port = value
                        actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))
                        flag = 1

            # Forwarding to the hosts
            if dpid in self.m_graph['hosts']:
                for key,value in self.m_graph['hosts'][dpid].items():
                    if value != in_port:
                        if LOG_DEBUG:
                            print str(dpid) + " | Host: " + str(key) + " <-> Port : " + str(value)
                        out_port = value
                        actions.append(datapath.ofproto_parser.OFPActionOutput(out_port))
                        flag = 1

            if flag:
                data = None
                if msg.buffer_id == ofproto.OFP_NO_BUFFER:
                    data = msg.data
                out = datapath.ofproto_parser.OFPPacketOut( datapath=datapath, buffer_id=msg.buffer_id, in_port=in_port, actions=actions, data=data)
                datapath.send_msg(out)

    # Host Add Event
    @set_ev_cls(event.EventHostAdd, MAIN_DISPATCHER)
    def update_topology_hostAdd(self, ev):
        mac = ev.host.mac
        dpid = ev.host.port.dpid

        # Switch's host information
        host_list = get_host(self.m_topology_api_app,dpid)
        for item in host_list:
            mac = item.mac
            dpid = item.port.dpid
            port = item.port.port_no

            # TODO : Improve for multiple Host connected to the Switch
            if port != 1:
                return

            # To ignore the MiddleBox as a Hosts
            if mac in set([MB1_MAC_ADDR, MB2_MAC_ADDR, MB3_MAC_ADDR]):
                print "Discarding MiddleBoxes as Hosts"
                return
            
            if mac not in self.m_graph['hosts'][dpid]:
                self.m_graph['hosts'][dpid].setdefault(mac,{})

            self.m_graph['hosts'][dpid][mac]=port

            # Used by Service Chain algorithm to calculate path
            self.m_mac_to_dpid[mac] = dpid

            if LOG_DEBUG:
                print "Saved Host information : " + str(mac) + " : " + str(dpid)

        # Update network's Spanning Tree
        if LOG_DEBUG:
            print "Event: Host Added - Re-calcuting Spanning Tree"
        for datapath in self.m_graph['switches']:
            self.prim_spanningTree(datapath.id)
            break


    # Link add event
    @set_ev_cls(event.EventLinkAdd)
    def update_topology_linkAdd(self, ev):
        link = ev.link
        # Switches inter-connections
        self.m_graph['edges'][link.src.dpid][link.dst.dpid] = link.src.port_no
        self.m_graph['edges'][link.dst.dpid][link.src.dpid] = link.dst.port_no

        if LOG_DEBUG:
            print "Saved Link Information: " + str(link.src.dpid) + " <-> " + str(link.dst.dpid)
            print "Event: Link Added - Re-calcuting Spanning Tree"

        # Update network's Spanning Tree
        for datapath in self.m_graph['switches']:
            self.prim_spanningTree(datapath.id)
            break


    # Switch Enter Event
    @set_ev_cls(event.EventSwitchEnter)
    def update_topology_switchEnter(self, ev):
        switch_list = get_switch(self.m_topology_api_app, None)

        # Add Switch
        for switch in switch_list:
            dpid= switch.dp.id
            self.m_graph['switches'].add(switch.dp)
            
            if dpid not in self.m_graph['edges']:
                self.m_graph['edges'].setdefault(dpid, {})
            if dpid not in self.m_graph['hosts']:
                self.m_graph['hosts'].setdefault(dpid,{})

    # Switch Leave event
    @set_ev_cls(event.EventSwitchLeave)
    def update_topology_switchLeave(self, ev):
        if LOG_DEBUG:
            print "Event: Switch Deleted - Re-calculating Spanning Tree"
        switch_list = get_switch(self.m_topology_api_app, None)

        # TODO: Improve the logic
        for mem_switch_dp in self.m_graph['switches']:
            flag = 0
            for switch in switch_list:
                if switch.dp.id == mem_switch_dp.id:
                    flag = 1
            
            if flag == 0:
                dpid = mem_switch_dp.id
                break

        # Remove Switch entries
        self.m_graph['switches'].remove(mem_switch_dp)
        for other_dpid,linked_dpid in self.m_graph['edges'].items():
            for link,port in linked_dpid.items():
                if link == dpid:
                    self.m_graph['edges'][other_dpid].pop(link)
        
        if dpid in self.m_graph['edges']:
            self.m_graph['edges'].pop(dpid,None)
            print self.m_graph['edges']
        
        if dpid in self.m_graph['hosts']:
            self.m_graph['hosts'].pop(dpid,None)
        if dpid in self.m_mac_to_port:
            self.m_mac_to_port.pop(dpid,None)

        # Update network's Spanning Tree
        for datapath in self.m_graph['switches']:
            self.prim_spanningTree(datapath.id)
            break

    # Updates the controller's view of switches in network topology
    def prim_spanningTree(self, src):
        self.m_treeLinks = {}
        queue = deque()
        color = {}

        for node in self.m_graph['switches']:
            self.m_treeLinks[node.id] = set()
            color[node.id] = COLOR_WHITE

        queue.append(src)

        while queue:
            u = queue.popleft()
            color[u] = COLOR_BLACK
            for v,port in self.m_graph['edges'][u].items():
                # Improve by using weights among the edges
                # Boils down to simple BFS
                if color[v] == COLOR_WHITE:
                    self.m_treeLinks[v].add(u)
                    self.m_treeLinks[u].add(v)
                    color[v] = COLOR_BLACK
                    queue.append(v)
        
        if LOG_DEBUG:
            print "Spanning Tree: " 
            for u in self.m_graph['switches']:
                print  str(u.id) + " <-> " + str(self.m_treeLinks[u.id])


    def DFS_visit(self, src_dpid, dst_dpid, color, path_list):
        u = src_dpid

        for v, port in self.m_graph['edges'][u].items():
            if v in self.m_treeLinks and u in self.m_treeLinks[v]:
                if color[v] == COLOR_WHITE:
                    color[v] = COLOR_BLACK

                    # Found dst_dpid
                    if v == dst_dpid:
                        path_list = [dst_dpid]
                        return SUCCESS, path_list
                    else:
                        ret_flag, ret_path_list = self.DFS_visit(v, dst_dpid, color, path_list)
                        if ret_flag == SUCCESS:
                            ret_path_list.append(v)
                            return ret_flag, ret_path_list

        return FAILURE, None

    def get_flow_path(self, host1, host2):
        # Sanity Check
        if host1 not in self.m_mac_to_dpid or host2 not in self.m_mac_to_dpid:
            print "Programming Error. This scenario should not occur."
            return None

        dpid1 = self.m_mac_to_dpid[host1]
        dpid2 = self.m_mac_to_dpid[host2]

        # Run DFS to get the flow-path
        color = {}
        path_list = []

        for u in self.m_graph['switches']:
            color[u.id] = COLOR_WHITE

        color[dpid1] = COLOR_BLACK
        path_list.append(dpid1)

        ret_flag, ret_path_list = self.DFS_visit(dpid1, dpid2, color, path_list)
        ret_path_list.append(dpid1)

        return ret_path_list
