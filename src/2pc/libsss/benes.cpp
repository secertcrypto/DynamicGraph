// Adapted from secertcrypto/libSSS, commit b16b0c467f915d297158594f68ecad25f371acf6.
// Original: osn/benes.cpp. Routing only; no upstream randomness or networking.
#include "benes.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stack>
#include <stdexcept>
using namespace std;
namespace graphiti::twopc::libsss {
Benes::Benes(const vector<int>& destination) {
    const auto n=destination.size();
    if(n<2 || (n&(n-1))) throw invalid_argument("Beneš size must be power of two >= 2");
    vector<bool> seen(n);
    for(auto x:destination) {if(x<0 || size_t(x)>=n || seen[x]) throw invalid_argument("invalid permutation"); seen[x]=true;}
    int depth=0; for(auto v=n;v>1;v>>=1) ++depth;
    perm.resize(n);inv_perm.resize(n);path.resize(n);
    switched.assign(2*depth-1,vector<int>(n/2));
    vector<int> source(n);iota(source.begin(),source.end(),0);
    gen_benes_route(depth,0,0,source,destination);
}
void Benes::DFS(int idx, int route)
{
	stack<pair<int, int>> st;
	st.push({idx, route});
	pair<int, int> pr;
	while (!st.empty())
	{
		pr = st.top();
		st.pop();
		path[pr.first] = pr.second;
		if (path[pr.first ^ 1] < 0) // if the next item in the vertical array is unassigned
			st.push({pr.first ^ 1,
					 pr.second ^ 1}); /// the next item is always assigned the opposite of this item,
									  /// unless it was part of path/cycle of previous node

		idx = perm[inv_perm[pr.first] ^ 1];
		if (path[idx] < 0)
			st.push({idx, pr.second ^ 1});
	}
}

int shuffle(int i, int n) { return ((i & 1) << (n - 1)) | (i >> 1); }

// n <- number of layers in the network (initialize as int(ceil(log2(src.size()))) )
void Benes::gen_benes_route(int n, int lvl_p, int perm_idx, const vector<int> &src,
							const vector<int> &dest)
{
	int levels, i, j, x, s;
	vector<int> bottom1;
	vector<int> top1;
	int values = src.size();
	// printf("values: %d, location: %d %d \n", values,lvl_p,perm_idx);

	/*
	 * daca avem doar un nivel in retea
	 */
	if (values == 2)
	{
		if (n == 1)
			switched[lvl_p][perm_idx] = src[0] != dest[0];
		else
			switched[lvl_p + 1][perm_idx] = src[0] != dest[0];
		return;
	}

	if (values == 3)
	{
		if (src[0] == dest[0])
		{
			switched[lvl_p][perm_idx] = 0;
			switched[lvl_p + 2][perm_idx] = 0;
			if (src[1] == dest[1])
				switched[lvl_p + 1][perm_idx] = 0;
			else
				switched[lvl_p + 1][perm_idx] = 1;
		}

		if (src[0] == dest[1])
		{
			switched[lvl_p][perm_idx] = 0;
			switched[lvl_p + 2][perm_idx] = 1;
			if (src[1] == dest[0])
				switched[lvl_p + 1][perm_idx] = 0;
			else
				switched[lvl_p + 1][perm_idx] = 1;
		}

		if (src[0] == dest[2])
		{
			switched[lvl_p][perm_idx] = 1;
			switched[lvl_p + 1][perm_idx] = 1;
			if (src[1] == dest[0])
				switched[lvl_p + 2][perm_idx] = 0;
			else
				switched[lvl_p + 2][perm_idx] = 1;
		}
		// std::cout<<"based case 3:"<<switched[lvl_p][perm_idx]<<" "<<switched[lvl_p+1][perm_idx]<<"
		// "<<switched[lvl_p+2][perm_idx];
		return;
	}

	/*
	 * aflam dimensiunea retelei benes
	 */
	levels = 2 * n - 1;

	vector<int> bottom2(values / 2);
	vector<int> top2(int(ceil(values * 0.5)));

	/*
	 * destinatia este o permutare a intrari
	 */

	for (i = 0; i < values; ++i)
		inv_perm[src[i]] = i;

	for (i = 0; i < values; ++i)
		perm[i] = inv_perm[dest[i]];

	for (i = 0; i < values; ++i)
		inv_perm[perm[i]] = i;

	/*
	 * cautam sa vedem ce switch-uri vor fi activate in partea
	 * inferioara a retelei
	 */
	fill(path.begin(), path.end(), -1);
	// memset(path, -1, sizeof(path));
	if (values % 2 == 1)
	{
		path[values - 1] = 1;
		path[perm[values - 1]] = 1;
		if (perm[values - 1] != values - 1)
		{
			int idx = perm[inv_perm[values - 1] ^ 1];
			DFS(idx, 0);
		}
	}

	for (i = 0; i < values; ++i)
		if (path[i] < 0)
		{
			DFS(i, 0);
		}

	/*
	 * calculam noile perechi sursa-destinatie
	 * 1 pentru partea superioara
	 * 2 pentru partea inferioara
	 */
	// partea superioara
	for (i = 0; i < values - 1; i += 2)
	{
		switched[lvl_p][perm_idx + i / 2] = path[i];
		for (j = 0; j < 2; ++j)
		{
			x = shuffle((i | j) ^ path[i], n);
			if (x < values / 2)
				bottom1.push_back(src[i | j]);
			else
				top1.push_back(src[i | j]);
			// input_wires[lvl_p][perm_idx + i / 2][j] = src[i | j];
		}
	}
	if (values % 2 == 1)
	{
		top1.push_back(src[values - 1]);
		// cout<<"pushing source: "<<src[values]<<"\n";
	}

	// partea inferioara
	for (i = 0; i < values - 1; i += 2)
	{
		s = switched[lvl_p + levels - 1][perm_idx + i / 2] = path[perm[i]];
		for (j = 0; j < 2; ++j)
		{
			x = shuffle((i | j) ^ s, n);
			if (x < values / 2)
				bottom2[x] = src[perm[i | j]];
			else
			{
				top2[i / 2] = src[perm[i | j]];
				// std:cout<<"top2 index "<<x - values / 2<<" : "<<src[perm[i | j]]<<std::endl;
				// top2[x - values / 2]
			}
			// input_wires[lvl_p + levels - 1][perm_idx + i / 2][j] = src[perm[i | j]];
		}
	}

	int idx = int(ceil(values * 0.5));
	if (values % 2 == 1)
	{
		// for (int i=0; i <= idx-2; ++i)
		//  top2[i] = top2[i+1];
		// printf("top2 added %d at location %d \n",dest[values-1], idx);
		top2[idx - 1] = dest[values - 1];
		// cout<<"pushing destination: "<<dest[values];
	}

	/*
	std::cout<<"sizes of vectors - bottom 1: "<<bottom1.size()
	<<" bottom 2: "<<bottom2.size()<<" top 1: "<<top1.size()
	<<"top 2: "<<top2.size();
	*/

	/*
	 * recursivitate prin partea superioara si inferioara
	 */
	gen_benes_route(n - 1, lvl_p + 1, perm_idx, bottom1, bottom2);
	gen_benes_route(n - 1, lvl_p + 1, perm_idx + values / 4, top1, top2);
}


}
