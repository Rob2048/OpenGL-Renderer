#ifdef ENABLE_SPHERICAL_HARMONICS
	layout(binding = 10) uniform sampler2D texSH;

	const float shconst[5] =
	{
		0.282095,
		0.488603,
		1.092548,
		0.315392,
		0.546274,
	};

	vec3 GetSHCoefficient(int Index){
	    return texelFetch(texSH, ivec2(Index, 0), 0).rgb;
	}

	vec3 EvalSH(vec3 normal)
	{
	    float x = normal.x;
	    float y = normal.y;
	    float z = normal.z;
	    
	    vec3 l00 = GetSHCoefficient(0);
	    
	    vec3 l10 = GetSHCoefficient(1);
	    vec3 l11 = GetSHCoefficient(2);
	    vec3 l12 = GetSHCoefficient(3);

	    vec3 l20 = GetSHCoefficient(4);
	    vec3 l21 = GetSHCoefficient(5);
	    vec3 l22 = GetSHCoefficient(6);
	    vec3 l23 = GetSHCoefficient(7);
	    vec3 l24 = GetSHCoefficient(8);
	    
	    vec3 result = (
	        l00 * shconst[0] +

	        l12 * shconst[1] * x +
	        l10 * shconst[1] * y + 
	        l11 * shconst[1] * z +

	        l20 * shconst[2] * x*y +
	        l21 * shconst[2] * y*z +
	        l22 * shconst[3] * (3.0*z*z - 1.0) +
	        l23 * shconst[2] * x*z +
	        l24 * shconst[4] * (x*x - y*y)
	    );

	    return toLinear(max(result, vec3(0.0)) * 10.0);
	    //return (max(result, vec3(0.0)));
	}

	void AddLightToSH(vec3 Color, inout vec3 SH[9], vec3 Dir)
	{
		SH[0] += Color;

		SH[1] += Color * Dir.x;
		SH[2] += Color * Dir.y;
		SH[3] += Color * Dir.z;

		SH[4] += Color * (Dir.x * Dir.z);
		SH[5] += Color * (Dir.z * Dir.y);
		SH[6] += Color * (Dir.y * Dir.x);

		SH[7] += Color * (3.0f * Dir.z * Dir.z - 1.0);

		SH[8] += Color * (Dir.x * Dir.x - Dir.y * Dir.y);
	}

	vec3 EvalSH(vec3 Dir, vec3 SH[9])
	{
		vec3 result = SH[0] * shconst[0];

		result += SH[1] * shconst[1] * -Dir.x;
		result += SH[2] * shconst[1] * -Dir.y;
		result += SH[3] * shconst[1] * Dir.z;

		result += SH[4] * shconst[2] * -(Dir.x * Dir.z);
		result += SH[5] * shconst[2] * -(Dir.z * Dir.y);
		result += SH[6] * shconst[2] * (Dir.y * Dir.x);

		result += SH[7] * shconst[3] * (3.0f * Dir.z * Dir.z - 1.0);

		result += SH[8] * shconst[4] * (Dir.x * Dir.x - Dir.y * Dir.y);	

		return result;
	}
#endif