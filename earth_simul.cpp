//g++ -o earth_simul.o earth_simul.cpp `gsl-config --cflags --libs`


#include<iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <stdlib.h>
#include <string>
using namespace std;
#include <cmath>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>



const int N = 1000;
const int PREM_len = 187;
const int totalNeutrinos=10000000;
const int path_resolution = 1000; //km
int counter =0;
const double R_earth = 6371.;
double dz = 2*R_earth/N;
double dx = dz;
const double PI = 3.1415962589793238;
double abundanceU_BSE[3]={12., 20., 35.};
double abundanceTh_BSE[3]={43., 80., 140.};
//format = {cosm, geoch, geodyn}

double the_r(double x, double z, double t, char component){
    double the_r_z=R_earth-z;
    double the_r_x = x;
    double the_r_mag = sqrt(the_r_x*the_r_x + the_r_z*the_r_z);
    if (component=='x'){
      return x-t*300000*the_r_x/the_r_mag;
    }
    else if(component=='z'){
      return z+t*300000*the_r_z/the_r_mag;
    }
}

double get_r(double x, double y){
  return sqrt(x*x + y*y);
}


double density_polynomials(double radius){
    double x = radius/6371.;
    //inner core
    if( radius<= 1221.5){
        return 13.0885-8.8381*x*x;}
    //outer core
    else if (radius<=3480){
        return 12.5815-1.2638*x-3.6426*x*x-5.5281*x*x*x;}
    //lower mantle
    else if (radius <= 5701){
        return 7.9565-6.4761*x+5.5283*x*x-3.0807*x*x*x;}
    //transition zone
    else if (radius <= 5771){
        return 5.3197-1.4836*x;}
    else if (radius <= 5971){
        return 11.2494-8.0298*x;}
    else if (radius <= 6151){
        return 7.1089-3.8045*x;}
    //lvz + lid
    else if (radius <= 6346.6){
        return 2.6910+0.6924*x;}
    //crust
    else if (radius <= 6356){
        return 2.9;}
    else if (radius <= 6368){
        return 2.6;}
    //ocean
    else if (radius <= 6371){
        return 1.020;}
}
vector<double> calculateMantleAbundances(double c_mass, double m_mass, double t_mass, double abundance_isot[3], double crust_abundance_isot){
  vector <double> abundances;
  abundances.reserve(3);
  for(int i =0;i<3;i++){
    abundances.push_back((t_mass*abundance_isot[i] - c_mass*crust_abundance_isot)/m_mass);
  }
  return abundances;
}
vector<double> mantleAbundanceTh;
vector<double> copy_vector(vector<double> to_copy){
  vector<double> copy;
  copy.reserve(10);
  for(int n=0;n<10;n++){
    copy.push_back(to_copy[n]);
  }
  return copy;
}
vector< vector<double> > import_model(string filename){
  float rad, depth, density, Vpv, Vph, Vsv, Vsh, eta, Q_mu, Q_kappa;
  string line;
  ifstream infile(filename.c_str());
  int i = 0;
  vector< vector<double> > model_matrix;
  model_matrix.reserve(199);
  vector<double> last_row;
  last_row.reserve(10);
  while(getline(infile, line) && i<=199){
    istringstream splittable_line(line);
    string field;
    vector<double> row;
    row.reserve(10);
    while(getline(splittable_line, field, ',')){
      istringstream field_ss(field);
      double field_num;
      field_ss >> field_num;
      row.push_back(field_num);
    }
    if(i==0){
      model_matrix.push_back(row);
      last_row = copy_vector(row);
      counter++;
    }
    else if(row[0]<last_row[0]){
      model_matrix.push_back(row);
      last_row = copy_vector(row);
      counter++;
    }
    splittable_line.clear();
    i++;
  }
  return model_matrix;
}
void split_array(vector< vector<double> > to_split, double container[PREM_len], int comp, bool invert){
  if(invert){
    for(int i=0;i<PREM_len;i++){
      container[PREM_len-1-i]=to_split[i][comp];
    }
  }
  else{
    for(int i=0;i<PREM_len;i++){
      container[i]=to_split[i][comp];
    }  }
}


class RingNode{
  public:
    double x;
    double z;
    double r;
    double getRadius(){
      r = sqrt(x*x + z*z);
      return r;
    }
    bool isEarth;
    bool isSE;
    bool isCrust;
    bool isMantle;
    double mass;
    double massDensity;
    double solidAngle;
    double getSolidAngle(){
      solidAngle = 2*PI*(x*dx/((R_earth-z)*(R_earth-z) + x*x));
      return solidAngle;
    }
    double volume;
    double getVolume(){
      volume = 2*PI*x*dx*dz;
      return volume;
    }
    double abundanceU;
    double abundanceTh;
    double neutrinoFlux;
    double neutrinoThFlux;
    double neutrinoUFlux;
    double relativeNeutrinoTh;
    double relativeNeutrinoU;
    double relativeNeutrino;
    double neutrinosProduced;
    double neutrinosProducedU;
    double neutrinosProducedTh;
    double slope;
    vector<double> path;
    float pathLen;


};

class Planet{
  public:
    RingNode asArray[N/2][N];
    double totalMass;
    double crustMass;
    double mantleMass;
    double totalFlux;

    void initializeCoords(){
      for(int i =0 ; i<N/2;i++){
        for(int k = 0;k<N;k++){
          asArray[i][k].x=i*dx;
          asArray[i][k].z=-6371. + k*dz;
          double r = asArray[i][k].getRadius();
          if(r<6371){
            asArray[i][k].isEarth=1;
            float dk=1e3-k;
            float di=-i;
            if(i!=0){asArray[i][k].slope = dk/di;}
            if(r>3480){
              asArray[i][k].isSE=1;
              if(r>R_earth-32.5){
                asArray[i][k].isCrust=1;
                asArray[i][k].isMantle=0;
              }
              else{
                asArray[i][k].isCrust=0;
                asArray[i][k].isMantle=1;
              }
            }
            else{
              asArray[i][k].isSE=0;
            }
          }
          else{asArray[i][k].isEarth=0;}
          double dummy_sa = asArray[i][k].getSolidAngle();
          double dummy_vol = asArray[i][k].getVolume();
        }
      }
    }
    void initializeDensity(){
      /*
      vector< vector<double> > PREM_complete;
      PREM_complete = ::import_model("../Models/PREM_1s.csv");
      double radiusArray[PREM_len], densityArray[PREM_len];
      ::split_array(PREM_complete, radiusArray, 0, 1);
      ::split_array(PREM_complete, densityArray, 2, 1);
      gsl_interp_accel *acc = gsl_interp_accel_alloc();
      gsl_spline *spline = gsl_spline_alloc(gsl_interp_steffen, PREM_len);
      gsl_spline_init(spline, radiusArray, densityArray, PREM_len);
      */
      for(int i =0 ; i<N/2;i++){
        for(int k = 0;k<N;k++){
          if(asArray[i][k].isEarth){
            //asArray[i][k].massDensity=gsl_spline_eval(spline, asArray[i][k].r, acc);
            asArray[i][k].massDensity=density_polynomials(asArray[i][k].r);
            asArray[i][k].mass=asArray[i][k].massDensity*1e3*asArray[i][k].volume*1e9;
            totalMass+=asArray[i][k].mass;
            if(asArray[i][k].isCrust){
              crustMass+=asArray[i][k].mass;
            }
            else if(asArray[i][k].isMantle){
              mantleMass+=asArray[i][k].mass;
            }
          }
          else{asArray[i][k].massDensity=-1;}
        }
      }
      //gsl_spline_free(spline);
      //gsl_interp_accel_free(acc);
    }
    void initializeAbundanceCrust(){
      for(int i =0 ; i<N/2;i++){
        for(int k = 0;k<N;k++){
          if(asArray[i][k].isCrust){
            asArray[i][k].abundanceU=1.31; //ppb
            asArray[i][k].abundanceTh=5.61; //ppb
          }
          else{
            asArray[i][k].abundanceU=0; //%
            asArray[i][k].abundanceTh=0; //%
          }
        }
      }
    }
    void initializeAbundanceMantle(string key, string bse_model){
      int model;
      if(bse_model=="cosmo"){model=0;}
      else if(bse_model=="geoch"){model=1;}
      else if(bse_model=="geodyn"){model=2;}
      if(key=="unif"){
        for(int i =0 ; i<N/2;i++){
          for(int k = 0;k<N;k++){
            if(asArray[i][k].isMantle){
              asArray[i][k].abundanceU=calculateMantleAbundances(crustMass, mantleMass, crustMass+mantleMass, abundanceU_BSE, 1.31)[model]; //ppb
              asArray[i][k].abundanceTh=calculateMantleAbundances(crustMass, mantleMass, crustMass+mantleMass, abundanceTh_BSE, 5.61)[model]; //ppb
            }
          }
        }
      }
      else if(key=="two_layer"){
        double bulk_mantle_U = calculateMantleAbundances(crustMass, mantleMass, crustMass+mantleMass, abundanceU_BSE, 1.31)[model];
        double bulk_mantle_Th = calculateMantleAbundances(crustMass, mantleMass, crustMass+mantleMass, abundanceTh_BSE, 5.61)[model];
        double mantleMass_fraction = 0.1*mantleMass;
        double mass_count=0;
        double limit_rad;
        int n=0;
        do {
          mass_count+=4*PI*(asArray[n][500].r)*(asArray[n][500].r)*dx*1e3*1e9*asArray[n][500].massDensity;
          limit_rad=asArray[n][500].r;
          n++;
        } while(mass_count<=mantleMass_fraction);
        for(int i =0 ; i<N/2;i++){
          for(int k = 0;k<N;k++){
            if(asArray[i][k].isMantle && asArray[i][k].r>limit_rad){
              asArray[i][k].abundanceU=4.7; //ppb
              asArray[i][k].abundanceTh=13.7; //ppb
            }
            else if(asArray[i][k].isMantle && asArray[i][k].r<=limit_rad){
              asArray[i][k].abundanceU=(-4.7*(mantleMass-mass_count)+bulk_mantle_U*mantleMass)/mass_count; //ppb
              asArray[i][k].abundanceTh=(-13.7*(mantleMass-mass_count)+bulk_mantle_Th*mantleMass)/mass_count; //ppb
            }
          }
        }

      }
    }
    void initializeFluxes(){
      for(int i=0;i<N/2;i++){
        for(int k=0;k<N;k++){
          if(asArray[i][k].isSE){
            asArray[i][k].neutrinoUFlux=(asArray[i][k].abundanceU*1e-6)*(asArray[i][k].massDensity*1e3)*(6)*(4.916*1e-18)*(0.9927)/(238.051*1.661e-27)*(asArray[i][k].volume*1e9)*asArray[i][k].solidAngle;
            asArray[i][k].neutrinoThFlux=(asArray[i][k].abundanceTh*1e-6)*(asArray[i][k].massDensity*1e3)*(4)*(1.563*1e-18)*(1)/(232.038*1.661e-27)*(asArray[i][k].volume*1e9)*asArray[i][k].solidAngle;
            asArray[i][k].neutrinoFlux=  asArray[i][k].neutrinoUFlux+asArray[i][k].neutrinoThFlux;
            asArray[i][k].relativeNeutrinoU=asArray[i][k].neutrinoUFlux/asArray[i][k].neutrinoFlux;
            asArray[i][k].relativeNeutrinoTh=asArray[i][k].neutrinoThFlux/asArray[i][k].neutrinoFlux;
            totalFlux+=asArray[i][k].neutrinoFlux;
          }
          else{
            asArray[i][k].neutrinoUFlux=0;
            asArray[i][k].neutrinoThFlux=0;
            asArray[i][k].neutrinoFlux= 0;
            asArray[i][k].relativeNeutrinoU=0;
            asArray[i][k].relativeNeutrinoTh=0;
          }
        }
      }
      for(int i=0;i<N/2;i++){
        for(int k=0;k<N;k++){
          asArray[i][k].relativeNeutrino=asArray[i][k].neutrinoFlux/totalFlux;
          asArray[i][k].neutrinosProduced=  asArray[i][k].relativeNeutrino*totalNeutrinos;
          asArray[i][k].neutrinosProducedU=asArray[i][k].neutrinosProduced*asArray[i][k].relativeNeutrinoU;
          asArray[i][k].neutrinosProducedTh=asArray[i][k].neutrinosProduced*asArray[i][k].relativeNeutrinoTh;
        }
      }
    }
    void initializePaths(){
      for(int i=0;i<N/2;i++){
        for(int k=0;k<N;k++){
          if(asArray[i][k].isSE){
            double z, x;
            z=asArray[i][k].z;
            x=asArray[i][k].x;
            vector<double> path;
            float path_len = get_r(R_earth-z,x );
            float element_num = roundf(path_len/path_resolution);
            asArray[i][k].pathLen=element_num;
            path.reserve(element_num);
            double t=0;
            while(t<path_len/300000){
              asArray[i][k].path.push_back(density_polynomials(get_r(the_r(x, z, t, 'x'), the_r(x, z, t, 'y'))));
              t+=path_resolution/300000;
            }
          }
        }
      }
    }
    void initialize(string key, string bse_model){
      initializeCoords();
      initializeDensity();
      initializeAbundanceCrust();
      initializeAbundanceMantle(key, bse_model);
      initializeFluxes();
      initializePaths();
    }
};


int main(int argc, char const *argv[]) {
  Planet *earth = new Planet();

  earth->initialize("two_layer", "cosmo");
//  cout << earth->totalMass << endl;

  for(int k=0;k<N;k++){
    for(int i =0 ; i<N/2;i++){
      cout << earth->asArray[i][k].neutrinosProduced  << ',' ;
      }
      cout << 0 << endl;
    }

//cout << earth->asArray[200][500].invPath[0] << endl;
  return 0;
}
