# Author : Mrinal Aich
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from mininet.node import RemoteController
from mininet.cli import CLI

class SingleSwitchTopo(Topo):
    "Single switch connected to n hosts."
    def build(self, n=2):

        for h in range(n):
            switch = self.addSwitch('s%s' % (h + 1), dpid="0000000000000%s" % (h+1))
            host = self.addHost('h%s' % (h + 1), ip='192.168.75.%s' % (h+233), mac='00:00:00:00:00:0%s' % (h+1))
            #host = self.addHost('h%s' % (h + 1), ip='10.0.0.%s' % (h+1), mac='00:00:00:00:00:0%s' % (h+1))
            self.addLink(host, switch)
        
        if 1:
            # Test Case 1 : Simple Loop
            for h in range(n):
                self.addLink('s%s' % (h+1), 's%s' % (((h+1)%n) + 1))
        else:
            # Test Case 2 : Create a mesh-topology
            for iter1 in range(n):
                for iter2 in range(iter1+1, n):
                    self.addLink('s%s' % (iter1+1), 's%s' % (iter2+1))
        

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
    # To print useful information
    setLogLevel('info')
    simpleTest()
