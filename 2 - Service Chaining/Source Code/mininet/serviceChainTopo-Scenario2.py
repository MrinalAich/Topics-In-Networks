# Author : Mrinal Aich
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from mininet.node import RemoteController
from mininet.cli import CLI

'''
    Network Topology ( Ports - in brackets )

                       (1,2) M1         M2 (1,2)                           M3 (1,2)
                             \ \      / /                                 | | 
                              \ \    / /                                  | |
                         (3,4) \ \  / / (5,6)                             | | (3,4)            (1)
              S5---------------- S1---------------- S2 ------------------ S3--------------- S4 
         (1) /  (2)         (7)  /(1)            (1)    (2)            (1)  \  (5)        (2)  \ 
            /                   /                                            \                  \ 
           /                   /                                              \                  \ 
          H5(1)               H1 (1)                                        (1) H3           (1) H4

'''

NO_OF_MIDDLEBOXES = 3

class SingleSwitchTopo(Topo):
    "Single switch connected to n hosts."
    def build(self, n=2):

        # Python's range(N) generates 0..N-1
        for h in range(n):
            switch      = self.addSwitch('s%s' % (h + 1), dpid="0000000000000%s" % (h+1), protocols='OpenFlow13')
            host        = self.addHost('h%s' % (h + 1), ip='10.0.0.%s' % (h+1), mac='00:00:00:00:00:0%s' % (h+1))

        for h in range(NO_OF_MIDDLEBOXES):
            middlebox   = self.addMiddleBox('m%s' % (h + 1), ip='10.0.0.%s' % (h+11), mac='00:00:00:00:00:2%s' % (h+1))


        # Host - Switch Link
        self.addLink('h1', 's1', 1, 1) # S1's H1-Port : 1 
        self.addLink('h3', 's3', 1, 1) # S3's H3-Port : 1 
        self.addLink('h4', 's4', 1, 1) # S4's H4-Port : 1
        self.addLink('h5', 's5', 1, 1) # S4's H4-Port : 1

        # Switch - Switch Link
        self.addLink('s1', 's2', 2, 1) # S1's S2-Port : 2 & S2's S1-Port : 1
        self.addLink('s2', 's3', 2, 2) # S2's S3-Port : 2 & S3's S2-Port : 2

        # MiddleBox - Switch Link
        self.addLinkPair( 's1', 'm1', 3, 4, 1, 2 ) # S1's M1-Ports : 3,4 & M1's S1-Ports : 1,2
        self.addLinkPair( 's1', 'm2', 5, 6, 1, 2 ) # S1's M2-Ports : 5,6 & M2's S1-Ports : 1,2
        self.addLinkPair( 's3', 'm3', 3, 4, 1, 2 ) # S3's M3-Ports : 3,4 & M3's S3-Ports : 1,2

        # Additional Switch - Switch Links
        self.addLink('s3', 's4', 5, 2)
        self.addLink('s1', 's5', 7, 2)
        

def simpleTest():
    "Create and test a simple network"
    topo = SingleSwitchTopo(n=5)
    net = Mininet(topo, controller=None)
    " Remote Ryu Controller"
    c = RemoteController('c', '0.0.0.0', 6633)
    net.addController(c)
    net.start()
    print "Dumping host connections"
    dumpNodeConnections(net.hosts)
    CLI(net)
    #print "Testing network connectivity"
    #net.pingAll()
    net.stop()

if __name__ == '__main__':
    # Tell mininet to print useful information
    setLogLevel('info')
    simpleTest()
