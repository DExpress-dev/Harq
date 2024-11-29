
#include "polyfit.h"
#include <math.h>
#include <cmath>
#include <vector>

polyfit::polyfit()
{
}

polyfit::~polyfit(void)
{
}

double_vector polyfit::get_coefficient(std::vector<point> sample, int n)
{
	std::vector<double_vector> matFunX;  //矩阵方程
	std::vector<double_vector> matFunY;  //矩阵方程
	double_vector temp;
	double sum;
	int i, j, k;

	//正规方程X
	for(i=0; i<=n; i++)
	{
		temp.clear();
		for(j=0; j<=n; j++)
		{
			sum = 0;
			for( k = 0; k< static_cast<int>(sample.size()); k++)
				sum += pow(sample[k].x, j+i);
			temp.push_back(sum);
		}
		
		matFunX.push_back(temp);
	}


	//正规方程Y
	for(i=0; i<=n; i++)
	{
		temp.clear();
		sum = 0;
		for( k = 0; k < static_cast<int>(sample.size()); k++)
			sum += sample[k].y*pow(sample[k].x, i);
		temp.push_back(sum);
		
		matFunY.push_back(temp);
	}


	//矩阵行列式变换
	double num1, num2, ratio;
	for(i = 0; i < static_cast<int>(matFunX.size()) - 1; i++)
	{
		num1 = matFunX[i][i];
		for(j = i + 1; j < static_cast<int>(matFunX.size()); j++)
		{
			num2 = matFunX[j][i];
			ratio = num2/num1;

			for(k = 0; k < static_cast<int>(matFunX.size()); k++)
				matFunX[j][k] = matFunX[j][k]-matFunX[i][k]*ratio;

			matFunY[j][0] = matFunY[j][0]-matFunY[i][0]*ratio;
		}

	}


	//计算拟合曲线的系数
	double_vector coeff(matFunX.size(), 0);
	for(i = static_cast<int>(matFunX.size()) - 1; i >= 0; i--)
	{
		if(i == static_cast<int>(matFunX.size()) - 1)
			coeff[i] = matFunY[i][0]/matFunX[i][i];
		else
		{
			for(j = i + 1; j < static_cast<int>(matFunX.size()); j++)
				matFunY[i][0] = matFunY[i][0]-coeff[j]*matFunX[i][j];
			coeff[i] = matFunY[i][0]/matFunX[i][i];
		}
	}
	return coeff;
}

double_vector polyfit::poly_bandwidths(std::vector<point> sample, int n)
{
	//得到系数
	double_vector coefficient_vector = get_coefficient(sample, n);

	//得到新的数值
	double_vector poly_array;
	for (int k = 0; k < static_cast<int>(sample.size()); k++)
	{
		double poly_y = 0;
		for (int i = 0; i < static_cast<int>(coefficient_vector.size()); i++)
		{
			poly_y += coefficient_vector[i] * pow(sample[k].x, i);
		}
		poly_array.push_back(poly_y);
	}

	return poly_array;
}
