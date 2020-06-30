#include <iostream>

GradientDescentOptimizer::GradientDescentOptimizer(float lr) : learningRate_(lr) {}

void GradientDescentOptimizer::computeGradients(BaseNode *loss)
{
    // get node queue in level order traversal like BFS
    getNodeQueue(loss);
    std::vector<std::future<void>> ftrs;
    // calculate gradients
    while (!nodeQueue_.empty())
    {
        auto node = nodeQueue_.front();
        nodeQueue_.pop_front();
        ftrs.emplace_back(std::async(std::launch::deferred, [node] {  node->gradient(); }));
    }
    // wait for results
    for_each(ftrs.begin(), ftrs.end(), [](std::future<void> &ftr) { ftr.wait(); });

    std::cout << "gradients computed! " << std::endl;
}

template <typename T>
Minimizer<T> GradientDescentOptimizer::minimize(BaseNode *loss)
{
    // Instantiate a minimzer object and return it
    return Minimizer<T>(this, loss);
}

void GradientDescentOptimizer::getNodeQueue(BaseNode *loss)
{
    // Do BFS 
    std::deque<BaseNode *> nodeQueue;
    std::unordered_map<BaseNode *, bool> visitedNodes;
    nodeQueue.push_front(loss);
    while (!nodeQueue.empty())
    {
        BaseNode *node = nodeQueue.front();
        nodeQueue_.push_back(node);
        nodeQueue.pop_front();
        visitedNodes[node] = true;
        auto nodes = node->getInputs();
        // go through all inputs of the node
        for (auto n : nodes)
        {
            // check if the node is visited before
            if (visitedNodes[n] != true)
            {
                // if node not visited add to queue
                nodeQueue.push_back(n);
                // if variable add to variable list
                if (n->getNodeType() == nodeType::variable)
                {
                    variableNodesList_.push_back(n);
                }
            }
        }
    }
}