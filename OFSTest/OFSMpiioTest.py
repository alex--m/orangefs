#!/usr/bin/python
##
#
# @namespace OFSMpiioTest
#
# @brief This class implements mpi-io tests to be run on the virtual file system.
#
#
# @var  header 
# Name of header printed in output file
# @var  prefix  
# Name of prefix for test name
# @var  run_client  
# False
# @var  mount_fs  
# Does the file system need to be mounted?
# @var  mount_as_fuse
# Do we mount it via fuse?
# @var  tests  
# List of test functions (at end of file)
#
#


header = "OFS MPI-IO Test"
prefix = "mpiio"
mount_fs = False
run_client = False
mount_as_fuse = False

def functions(self,testing_network):
    pass
def heidleberg_IO(self,testing_network):
    pass
def ior_mpiio(self,testing_network):
    pass
def ior_mpiio_3(self,testing_network):
    pass
def noncontig(self,testing_network):
    pass
def romio_async(self,testing_network):
    pass
def romio_coll_test(self,testing_network):
    pass
def romio_error(self,testing_network):
    pass
def romio_excl(self,testing_network):
    pass
def romio_file_info(self,testing_network):
    pass
def romio_noncontig_coll2(self,testing_network):
    pass
def romio_psimple(self,testing_network):
    pass
def romio_simple(self,testing_network):
    pass
def romio_split_coll(self,testing_network):
    pass
def romio_status(self,testing_network):
    pass
def stadler_file_view_test(self,testing_network):
    pass
    
##
#
# @fn romio_testsuite(testing_node,output=[]):
#
# @brief This is the romio testsuite that comes with mpich and can also be used with OpenMPI. 
# This does not use pbs/torque, but uses the MPI process managers directly.
# @param testing_node OFSTestNode on which tests are run.
# @param output Array that holds output from commands. Passed by reference. 
#   
# @return 0 Test ran successfully
# @return Not 0 Test failed
#
#

def romio_testsuite(testing_node,output=[]):

    #/opt/mpi/openmpi-1.6.5/ompi/mca/io/romio/romio/test
    testing_node.changeDirectory("/opt/mpi/openmpi-1.6.5/ompi/mca/io/romio/romio/test")
    
    rc = 0
    print "%s -machinefile=%s -fname=%s/romioruntests" % (testing_node.romio_runtests_pvfs2,testing_node.created_openmpihosts,testing_node.ofs_mount_point)
    rc = testing_node.runSingleCommand("%s -machinefile=%s -fname=%s/romioruntests" % (testing_node.romio_runtests_pvfs2,testing_node.created_openmpihosts,testing_node.ofs_mount_point),output)
    
    #TODO: Compare actual results with expected.
    
    return rc

tests = [ romio_testsuite ]


