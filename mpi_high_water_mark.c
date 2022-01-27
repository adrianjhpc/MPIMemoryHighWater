#include <sys/types.h>
#include <unistd.h>
#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define STRING_LENGTH 1024

int mpi_high_water_name_to_colour(const char *);
int mpi_high_water_get_key();

// Function to intercept Fortran MPI call to MPI_FINALIZE. Passes through to the
// C version of MPI_Finalize following this routine.
void mpi_finalize_(int *ierr){
  *ierr = MPI_Finalize();
}

// Function to intercept MPI_Finalize, collect memory information, and then call the MPI
// library finalize using PMPI_Finalize.
int MPI_Finalize(){
  int ierr;
  int iter = 0;
  char hostname[STRING_LENGTH];
  char statusfile_path[STRING_LENGTH];
  char line[STRING_LENGTH];
  pid_t pid;
  FILE *statusfile;
  char *tokens;
  int node_key;
  int temp_size;
  int temp_rank;
  MPI_Comm temp_comm;
  typedef struct communicator {
    MPI_Comm comm;
    int rank;
    int size;
  } communicator;
  communicator node_comm, world_comm, root_comm;
  int memory_used, node_max, node_min, node_total, root_indivi_max, root_indivi_min, root_node_av, root_node_min, root_node_max;  

  // Get the rank and size of the node communicator this process is involved
  // in.
  MPI_Comm_size(MPI_COMM_WORLD, &temp_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &temp_rank);
  world_comm.size = temp_size;
  world_comm.rank = temp_rank;  


  world_comm.comm = MPI_COMM_WORLD;
  
  // Get a integer key for this process that is different for every node
  // a process is run on.
  node_key = mpi_high_water_get_key();
  
  // Use the node key to split the MPI_COMM_WORLD communicator
  // to produce a communicator per node, containing all the processes
  // running on a given node.
  MPI_Comm_split(world_comm.comm, node_key, 0, &temp_comm);
  
  // Get the rank and size of the node communicator this process is involved
  // in.
  MPI_Comm_size(temp_comm, &temp_size);
  MPI_Comm_rank(temp_comm, &temp_rank);
  
  node_comm.comm = temp_comm;
  node_comm.rank = temp_rank;
  node_comm.size = temp_size;
  
  // Now create a communicator that goes across nodes. The functionality below will
  // create a communicator per rank on a node (i.e. one containing all the rank 0 processes
  // in the node communicators, one containing all the rank 1 processes in the
  // node communicators, etc...), although we are really only doing this to enable
  // all the rank 0 processes in the node communicators to undertake collective operations.
  MPI_Comm_split(world_comm.comm, node_comm.rank, 0, &temp_comm);
  
  MPI_Comm_size(temp_comm, &temp_size);
  MPI_Comm_rank(temp_comm, &temp_rank);
  
  root_comm.comm = temp_comm;
  root_comm.rank = temp_rank;
  root_comm.size = temp_size;
  
  gethostname(hostname, STRING_LENGTH);
  
  pid = getpid();
  snprintf(statusfile_path, STRING_LENGTH, "/proc/%d/status", pid);
  
  if(statusfile = fopen(statusfile_path, "r")){
    
    while (fgets(line, STRING_LENGTH, statusfile)) {
      if(strstr(line,"VmPeak")){
	//              printf("%s:%d: %s",hostname,pid,line);
      }else if(strstr(line,"VmHWM")){
	tokens = strtok(line, " ");
	while (tokens != NULL){
	  if(iter == 1){
	    memory_used = atoi(tokens);
	    break;
	  }
	  tokens = strtok (NULL, " ");
	  iter++;
	}
      }
    }

    MPI_Reduce(&memory_used, &node_max, 1, MPI_INT, MPI_MAX, 0, node_comm.comm);
    MPI_Reduce(&memory_used, &node_min, 1, MPI_INT, MPI_MIN, 0, node_comm.comm);
    MPI_Reduce(&memory_used, &node_total, 1, MPI_INT, MPI_SUM, 0, node_comm.comm);
    if(node_comm.rank == 0){
      MPI_Reduce(&node_max, &root_indivi_max, 1, MPI_INT, MPI_MAX, 0, root_comm.comm);
      MPI_Reduce(&node_min, &root_indivi_min, 1, MPI_INT, MPI_MIN, 0, root_comm.comm);
      MPI_Reduce(&node_total, &root_node_max, 1, MPI_INT, MPI_MAX, 0, root_comm.comm);
      MPI_Reduce(&node_total, &root_node_min, 1, MPI_INT, MPI_MIN, 0, root_comm.comm);      
      MPI_Reduce(&node_total, &root_node_av, 1, MPI_INT, MPI_SUM, 0, root_comm.comm);
      root_node_av = root_node_av/root_comm.size;
    }
    if(world_comm.rank == 0){
      printf("process max %dMB min %dMB\n",root_indivi_max/1024, root_indivi_min/1024);
      printf("node max %dMB min %dMB avg %dMB\n",root_node_max/1024, root_node_min/1024, root_node_av/1024);
    }
    
  }else{
    printf("%s:%d problem opening /proc/pid/status file\n",hostname,pid);
  }
  return PMPI_Finalize();
}

// The routine convert a string (name) into a number
// for use in a MPI_Comm_split call (where the number is
// known as a colour). It is effectively a hashing function
// for strings but is not necessarily robust (i.e. does not
// guarantee it is collision free) for all strings, but it
// should be reasonable for strings that different by small
// amounts (i.e the name of nodes where they different by a
// number of set of numbers and letters, for instance
// login01,login02..., or cn01q94,cn02q43, etc...)
int mpi_high_water_name_to_colour(const char *name){
        int res;
        int multiplier = 131;
        const char *p;

        res = 0;
        for(p=name; *p ; p++){
                // Guard against integer overflow.
                if (multiplier > 0 && (res + *p) > (INT_MAX / multiplier)) {
                        // If overflow looks likely (due to the calculation above) then
                        // simply flip the result to make it negative
                        res = -res;
                }else{
                        // If overflow is not going to happen then undertake the calculation
                        res = (multiplier*res);
                }
                // Add on the new character here.
                res = res + *p;
        }
        // If we have ended up with a negative result, invert it to make it positive because
        // the functionality (in MPI) that we will use this for requires the int to be positive.
        if( res < 0 ){
                res = -res;
        }
        return res;
}

// Get an integer key for a process based on the name of the
// node this process is running on. This is useful for creating
// communicators for all the processes running on a node.
int mpi_high_water_get_key(){

        char name[MPI_MAX_PROCESSOR_NAME];
        int len;
        int lpar_key;

        MPI_Get_processor_name(name, &len);
        lpar_key = mpi_high_water_name_to_colour(name);

        return lpar_key;

}
