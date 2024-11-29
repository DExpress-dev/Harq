#pragma once

#ifndef polyfit_h
#define polyfit_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>
#include <map>
#include <list>
#include <vector>

#include "../../../header/write_log.h"

//方程式：y(x) = a0 + a1x^1 + a2x^2 + a3x^3 + a4x^4 
struct point
{
	double x;	
	double y;
};
typedef std::vector<double> double_vector;

class polyfit
{
public:
	polyfit();
	~polyfit(void);

private:
	double_vector get_coefficient(std::vector<point> sample, int n);

public:
	double_vector poly_bandwidths(std::vector<point> sample, int n);
	
    
};

#endif /* polyfit_h */

