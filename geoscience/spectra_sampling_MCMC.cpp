/*
g++ -fopenmp -o spectra_sampling_MCMC.o spectra_sampling_MCMC.cpp `gsl-config --cflags --libs`
*/
#include <iostream>
using namespace std;
#include <sstream>
#include <string>
#include <fstream>
#include <time.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <sys/time.h>
#include <mpi.h>

const int N = 10000000; //Number of samples
const gsl_rng_type *Gen_Type; //Type of random number generator to use.
gsl_rng *Gen; //The actual generator object.
gsl_spline *spectrum_spline;
gsl_interp_accel *acc;


void read_file_into_2D_array(string filename, double to_fill[4500][2]){
  //Fills the array to_fill with 4500 elements for energy and neutrino/energy.

  //Open the file filename
  ifstream spectrum_file(filename.c_str());
  //Strings to save the input as there's header.
  string x, y;
  int it=0; //Count the lines with header.
  //Read file line by line.
  while(spectrum_file >> y >> x){
    it++; //corresponds to iteration number.
    //When header has been read.
    if(it>=12 && it<4512){ //Avoid stack smashing!
      //Convert read strings in stringstreams.
      istringstream x_str(x);
      istringstream y_str(y);
      double x_num, y_num;
      //Cast stringstream into doubles.
      x_str >> x_num;
      y_str >> y_num;
      //Fill array.
      to_fill[it-12][0]=x_num/1e3; //MeV
      to_fill[it-12][1]=y_num*1e3; //1/MeV
      //Clear the stringstreams to avoid problems in next iteration.
      x_str.clear();
      y_str.clear();
    }
  }
}
void split_array(double to_split[4500][2], double to_return[4500], int comp){
  for(int k=0;k<4500;k++){
    to_return[k]=to_split[k][comp];
  }
}
double *mh_sampling(double spectrum_array[4500][2]){
  double *markov_chain = new double[N];
  double initial_value = ((4.5-0.0005)*gsl_rng_uniform(Gen)) + 0.0005; //Uniformly random sample in [0.0005, 4.5).
  markov_chain[0] = initial_value;
  //cout << initial_value << endl;

  for(int i=0;i<N-1;i++){
    double possible_jump;
    do {
      possible_jump = gsl_ran_gaussian(Gen, 0.1) + markov_chain[i];
    } while(possible_jump < 0.0005 || possible_jump>4.5);
    double criteria = gsl_spline_eval(spectrum_spline, possible_jump, acc)/gsl_spline_eval(spectrum_spline, markov_chain[i], acc);
    if(criteria>=1.){
      markov_chain[i+1]=possible_jump;
    //  cout << possible_jump << endl;
    }
    else{
      double other_random = gsl_rng_uniform(Gen);
      if(other_random<=criteria){
        markov_chain[i+1]=possible_jump;
      //  cout << possible_jump << endl;
      }
      else{
        markov_chain[i+1]=markov_chain[i];
        //cout << markov_chain[i] << endl;
      }
    }
  }
  return markov_chain;
}
int main(int argc, char const *argv[]) {
  MPI_Init(NULL, NULL);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  string filename="../Models/AntineutrinoSpectrum_all/AntineutrinoSpectrum_";
  string isotname;
  if(world_rank==0){
    isotname = "232Th.knt";
  }
  else if (world_rank==1){
    isotname = "238U.knt";
  }
  filename += isotname;
  struct timeval time;
  gettimeofday(&time,NULL);
  double spectrum[4500][2];
  read_file_into_2D_array(filename, spectrum);
  unsigned long int seed = (time.tv_sec * 1000) + (time.tv_usec / 1000);
  gsl_rng_env_setup(); //Setup environment variables.
  Gen_Type = gsl_rng_taus; //The fastest random number generator.
  Gen = gsl_rng_alloc(Gen_Type); //Allocate necessary memory, initialize generator object.
  gsl_rng_set(Gen, seed); //Seed the generator
  acc = gsl_interp_accel_alloc(); //Allocate memory for interpolation acceleration.
  spectrum_spline = gsl_spline_alloc(gsl_interp_cspline, 4500); //Allocate memory for spline object for cubic spline in array of length 4500.
  double x_arr[4500], y_arr[4500];
  split_array(spectrum, x_arr, 0);
  split_array(spectrum, y_arr, 1);
  double real_y_arr[4500];
  for(int n=0;n<4500;n++){
    real_y_arr[n]=x_arr[n]*y_arr[n];
  }
  gsl_spline_init(spectrum_spline, x_arr, real_y_arr, 4500); //Initialize spline object.
  double  real_spectrum[4500][2];
  for(int n = 0;n<4500;n++){
    real_spectrum[n][0]=x_arr[n];
    real_spectrum[n][1]=real_y_arr[n];
  }
  double *rand_sampl = mh_sampling(real_spectrum);
  gsl_rng_free(Gen);
  string savename = "energy_repo_";
  savename+=isotname;
  ofstream outfile;
  outfile.open(savename.c_str());
  for(int n = 0; n<N;n++){
    outfile << rand_sampl[n] << endl;
  }
  outfile.close();
  MPI_Finalize();
  return 0;
}
