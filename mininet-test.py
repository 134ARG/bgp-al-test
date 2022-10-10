#!/usr/bin/python

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.util import dumpNodeConnections
from mininet.log import setLogLevel
from time import sleep


class SingleSwitchTopo(Topo):
    "Single switch connected to n hosts."

    def build(self, n=2):
        # switch = self.addSwitch('s1')
        # # Python's range(N) generates 0..N-1
        # for h in range(n):
        #     host = self.addHost('h%s' % (h + 1))
        #     self.addLink(host, switch)
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        # h3 = self.addHost('h3')
        self.addLink(h1, h2)
        # self.addLink(h1, h3)


def execute_test_program(h1):
    print("Starting test...")
    h1.cmd('/tmp/test-client > /tmp/test.out')
    sleep(1)
    print("Stopping test")
    print("Reading output")
    f = open('/tmp/test.out')
    lineno = 1
    for line in f.readlines():
        print("\t%d:\t %s" % (lineno, line.strip()))
        lineno += 1
    f.close()


def simpleTest():
    "Create and test a simple network"
    topo = SingleSwitchTopo(n=4)
    net = Mininet(topo)
    net.start()
    execute_test_program(net.get('h1'))
    execute_test_program(net.get('h2'))
    # execute_test_program(net.get('h3'))
    print("Dumping host connections")
    dumpNodeConnections(net.hosts)
    print("Testing network connectivity")
    net.pingAll()
    net.stop()


if __name__ == '__main__':
    # Tell mininet to print useful information
    setLogLevel('info')
    simpleTest()
