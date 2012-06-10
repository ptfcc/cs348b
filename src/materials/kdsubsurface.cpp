/*
    pbrt source code Copyright(c) 1998-2010 Matt Pharr and Greg Humphreys.

    This file is part of pbrt.

    pbrt is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.  Note that the text contents of
    the book "Physically Based Rendering" are *not* licensed under the
    GNU GPL.

    pbrt is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#define VDB 1

#if VDB
#include "vdb-win/vdb.h"
#endif

// materials/kdsubsurface.cpp*
#include "stdafx.h"
#include "materials/kdsubsurface.h"
#include "textures/constant.h"
#include "volume.h"
#include "spectrum.h"
#include "reflection.h"
#include "texture.h"
#include "paramset.h"
//for temperature distribution
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <stdlib.h>


KdSubsurfaceMaterial::KdSubsurfaceMaterial(Reference<Texture<Spectrum> > kd,
            Reference<Texture<Spectrum> > kr,
            Reference<Texture<float> > mfp,
            Reference<Texture<float> > e,
            Reference<Texture<float> > bump) {

		Kd = kd;
        Kr = kr;
        meanfreepath = mfp;
        eta = e;
        bumpMap = bump;
		tempdist = gridX = gridY = gridZ = NULL;

		openTempDist("tempdist2");
		openGrid("gridX2", gridX);
		openGrid("gridY2", gridY);
		openGrid("gridZ2", gridZ);

		getMinMaxTemperatures();
		printf("Constructor - Max Temp: %.3f, Min Temp: %.3f\n", maxTemp, minTemp);
}


void KdSubsurfaceMaterial::getMinMaxTemperatures() {

	maxTemp = 0.f; 
	minTemp = DBL_MAX;
	for (int i = 0; i < nx; i++) {
	for (int j = 0; j < ny; j++) {
	for (int k = 0; k < nz; k++) {
		double temp = getTempdist(i,j,k);
		if (maxTemp < temp) maxTemp = temp;
		if (minTemp > temp)	minTemp = temp;
		//printf("temp: %.3f, Max Temp: %.3f, Min Temp: %.3f\n", temp, maxTemp, minTemp);
	}}}
}

unsigned int KdSubsurfaceMaterial::saveToArray(const std::string &txt, char ch)
{
    unsigned int pos = txt.find( ch );
	if(string::npos == pos) return 0;
    unsigned int initialPos = 0;


	//first two
	int i = atoi(txt.substr( initialPos, pos - initialPos + 1 ).c_str()) - 1;	
	initialPos = pos + 1;
    pos = txt.find( ch, initialPos );
	
	int j = atoi(txt.substr( initialPos, pos - initialPos + 1 ).c_str()) - 1;
	initialPos = pos + 1;
    pos = txt.find( ch, initialPos );

	int arrayIndex = 0;
    // Decompose statement
    while( pos != std::string::npos ) {
		setTempdist(i,j,arrayIndex, strtod(txt.substr( initialPos, pos - initialPos + 1 ).c_str(), NULL));
		arrayIndex++;
        initialPos = pos + 1;
		
        pos = txt.find( ch, initialPos );
    }

    // Add the last one
    setTempdist(i,j,arrayIndex, strtod(txt.substr( initialPos, pos - initialPos + 1 ).c_str(), NULL));

    return arrayIndex;
}

void KdSubsurfaceMaterial::openGrid(const char* file, double* grid){
	string line;
	std::ifstream myfile (file);
	int linenumber = 0;
	if (myfile.is_open())
	{
		while ( myfile.good() )
		{
			getline (myfile,line);
			if(line.length() < 3) break; //the last line of file was an empty line. If that's the case, finish reading
			grid[linenumber] = strtod(line.c_str(), NULL);
			linenumber++;
		}
		myfile.close();
	}
	else std::cout << "Unable to open grid file\n";

}

void KdSubsurfaceMaterial::initializeArrays(int nx, int ny, int nz) {
	gridX = new double[nx];
	gridY = new double[ny];
	gridZ = new double[nz];

	tempdist = new double[nx*ny*nz];
}

int KdSubsurfaceMaterial::openTempDist(const char* file){
    string line;
	std::ifstream myfile (file);
	int linenumber = 0;
	if (myfile.is_open())
	{		
		getline(myfile, line);
		nx = atoi(line.c_str());
		
		getline(myfile, line);
		ny = atoi(line.c_str());
		
		getline(myfile, line);
		nz = atoi(line.c_str());
		
		initializeArrays(nx, ny, nz);
		
		while ( myfile.good() )
		{
			getline (myfile,line);
			if(line.length() < 3) break; //the last line of file was an empty line. If that's the case, finish reading
			linenumber++;
			saveToArray(line, '\t');

		}
		myfile.close();
	}	else std::cout << "Unable to open file"; 

	return 0;
}


// KdSubsurfaceMaterial Method Definitions
BSDF *KdSubsurfaceMaterial::GetBSDF(const DifferentialGeometry &dgGeom,
              const DifferentialGeometry &dgShading,
              MemoryArena &arena) const {
    // Allocate _BSDF_, possibly doing bump mapping with _bumpMap_
    DifferentialGeometry dgs;
    if (bumpMap)
        Bump(bumpMap, dgGeom, dgShading, &dgs);
    else
        dgs = dgShading;
    BSDF *bsdf = BSDF_ALLOC(arena, BSDF)(dgs, dgGeom.nn);
	
	// Evaluate textures for _MatteMaterial_ material and allocate BRDF
    Spectrum r = Kd->Evaluate(dgs).Clamp();
    bsdf->Add(BSDF_ALLOC(arena, Lambertian)(r));
    
	return bsdf;
}


BSSRDF *KdSubsurfaceMaterial::GetBSSRDF(const DifferentialGeometry &dgGeom,
              const DifferentialGeometry &dgShading,
              MemoryArena &arena) const {
	//return NULL;

	
	assert(tempdist != NULL && gridX != NULL && gridY != NULL && gridZ != NULL);
	//printf("dgGeom (%.3f, %.3f, %.3f)\n", dgGeom.p.x, dgGeom.p.y, dgGeom.p.z);
    float e = eta->Evaluate(dgShading);
    float mfp = meanfreepath->Evaluate(dgShading);
    Spectrum kd = Kd->Evaluate(dgShading).Clamp();
    Spectrum sigma_a, sigma_prime_s;
    SubsurfaceFromDiffuse(kd, mfp, e, &sigma_a, &sigma_prime_s);	
	
    BSSRDF* bssrdf = BSDF_ALLOC(arena, BSSRDF)(sigma_a, sigma_prime_s, e);
	bssrdf->mult = CoefficientSpectrum<3>(1.f);

	Point p_obj = (*dgGeom.shape->WorldToObject)(dgGeom.p);
	//printf("%f %f %f -> %f %f\n", p_obj.x, p_obj.y, p_obj.z, dgGeom.u, dgGeom.v);
	int i = Clamp((int)((p_obj.x - gridX[0]) / (gridX[1] - gridX[0])) + 1, 0, nx-1);
	int j = Clamp((int)((p_obj.y - gridY[0]) / (gridY[1] - gridY[0])) + 1, 0, ny-1);
	int k = Clamp((int)((p_obj.z - gridZ[0]) / (gridZ[1] - gridZ[0])) + 1, 0, nz-1);

	
		#if VDB
	vdb_color(dgShading.u, dgShading.v, 0.0f);
	vdb_point(p_obj.x, p_obj.y, p_obj.z);
	return NULL;
#endif
	double temp = getTempdist(i,j,k);
	// scale temp to normal charcoal burning temperatures
	//assert(maxTemp > 0.f || minTemp > 0g.f);
	temp = (temp-minTemp)*3000.f/(maxTemp-minTemp);
	//printf("temp(%d, %d, %d) = %.3f\n", i, j, k, temp);

	float vals[3] = {1.0f, 1.0f, 1.0f};
	
	if (temp < 500.f) 
		// no pyrolysis, don't even do subsurface scattering
		return NULL; 

	if(temp >= 500.0f){	
		float rgb[3] = {700, 530, 470};
		Blackbody(rgb, 3, temp, vals);

		bssrdf->mult = RGBSpectrum::FromRGB(vals);
	}
	//bssrdf->mult.Print(stdout); printf("\n");


	

	return bssrdf;
}


KdSubsurfaceMaterial *CreateKdSubsurfaceMaterial(const Transform &xform,
        const TextureParams &mp) {
  
    Reference<Texture<Spectrum> > kd = mp.GetSpectrumTexture("Kd", Spectrum(.5f));
    Reference<Texture<float> > mfp = mp.GetFloatTexture("meanfreepath", 1.f);
    Reference<Texture<float> > ior = mp.GetFloatTexture("index", 1.3f);
    Reference<Texture<Spectrum> > kr = mp.GetSpectrumTexture("Kr", Spectrum(1.f));
    Reference<Texture<float> > bumpMap = mp.GetFloatTexture("bumpmap", 0.f);
    return new KdSubsurfaceMaterial(kd, kr, mfp, ior, bumpMap);
}

