#pragma once
#include <vector>
namespace graphiti::twopc::libsss {
// Pull convention: output[i] = input[destination[i]]. Private to its owner.
class Benes {
    std::vector<int> perm,inv_perm;
    std::vector<signed char> path;
    void DFS(int idx,int route);
    void gen_benes_route(int n,int level,int offset,const std::vector<int>& src,const std::vector<int>& dest);
public:
    std::vector<std::vector<int>> switched;
    explicit Benes(const std::vector<int>& destination);
};
}
