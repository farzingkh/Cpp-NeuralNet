#include "../include/operation.h"
#include <iostream>

// --- BaseNode ---

void BaseNode::setName(std::string n)
{
    std::lock_guard<std::mutex> lck(Mtx_);
    _name = n;
}

void BaseNode::addInputs(BaseNode *n)
{
    std::lock_guard<std::mutex> lck1(Mtx_);
    for (int i = 0; i < _inputs.size(); i++)
    {
        if (_inputs[i].get() == nullptr)
        {
            _inputs[i].reset(n);
            return;
        }
    }
    _inputs.push_back(Locking_ptr<BaseNode>(n));
}

void BaseNode::eraseInput(BaseNode *n)
{
    std::lock_guard<std::mutex> lck(Mtx_);
    // remove the input node but keep the place
    for (int i = 0; i < _inputs.size(); i++)
    {
        // remove the node and use nullptr as a placeholder
        if (_inputs[i] == n)
        {
            _inputs[i].reset(nullptr);
        }
    }
}

void BaseNode::addConsumers(BaseNode *n)
{
    std::lock_guard<std::mutex> lck(Mtx_);
    // remove consumer but keep the place
    for (int i = 0; i < _consumers.size(); i++)
    {
        // check if there is deleted consumer
        if (_consumers[i].get() == nullptr)
        {
            // replace the deleted consumer in place
            _consumers[i].reset(n);
            return;
        }
    }
    // add consumer and increment the size
    _consumers.push_back(Locking_ptr<BaseNode>(n));
    consumerSize_++;
}

void BaseNode::eraseConsumer(BaseNode *n)
{
    std::lock_guard<std::mutex> lck(Mtx_);
    // remove consumer but keep the place
    for (int i = 0; i < _consumers.size(); i++)
    {
        // remove the node and use nullptr as a placeholder
        if (_consumers[i] == n)
        {
            _consumers[i].reset(nullptr);
        }
    }
}

template <typename T>
T BaseNode::getValue()
{
    std::unique_lock<std::mutex> lck(Mtx_);
    auto node = static_cast<Node<T> *>(this);
    lck.unlock();
    return node->getValue();
}

template <typename T>
T BaseNode::getGradient()
{
    std::unique_lock<std::mutex> lck(Mtx_);
    auto node = static_cast<Node<T> *>(this);
    lck.unlock();
    return node->getGradient();
}

template <typename T>
void BaseNode::setGrad(T t)
{
    std::unique_lock<std::mutex> lck(Mtx_);
    auto node = static_cast<Node<T> *>(this);
    lck.unlock();
    node->setGrad(t);
}

std::string BaseNode::getName()
{
    std::lock_guard<std::mutex> lck(Mtx_);
    return _name;
}

std::vector<Locking_ptr<BaseNode>> BaseNode::getInputs()
{
    // return a copy to avoid data races
    std::lock_guard<std::mutex> lck(Mtx_);
    return _inputs;
}

std::vector<Locking_ptr<BaseNode>> BaseNode::getConsumers()
{
    // return a copy to avoid data races
    std::lock_guard<std::mutex> lck(Mtx_);
    return _consumers;
}

nodeType BaseNode::getNodeType()
{
    std::lock_guard<std::mutex> lck(Mtx_);
    return _nType;
}

operationType BaseNode::getOperationType()
{
    std::lock_guard<std::mutex> lck(Mtx_);
    return _opType;
}

// --- Node  ---

template <typename T>
T Node<T>::getValue()
{
    std::lock_guard<std::mutex> lck(nodeMtx_);
    //std::cout << "Variable get value..." << std::endl;
    if (_dataAvailable.load())
    {
        //std::cout << "Output get: " << *_output << ", size: " << (*_output).rows() << "," << (*_output).cols() << std::endl;
        // return value to avoid data race complications with pointers
        return *_output;
    }
    else
    {
        //std::cout << "Data not available" << std::endl;
        return T();
    }
}

template <typename T>
T Node<T>::getGradient()
{
    // lock for all _grad and _output direct read and write
    std::unique_lock<std::mutex> lck1(nodeMtx_);
    //std::cout << "Get gradient ...\n";
    //std::cout << "Thread ID: " << std::this_thread::get_id() << std::endl;
    // Initialize node's gradient
    T grad;
    // check if node has a consumer; consumerSize_ is atomic
    int cnsSize = this->consumerSize_.load();
    if (cnsSize > 0)
    {
        // wait until gradient data is available
        if ((this->_gradientAvailable.load()))
        {
            //std::cout << "No wait required; Thread ID: " << std::this_thread::get_id() << std::endl;
            grad.setZero(_grad[0]->rows(), _grad[0]->cols());
            //  get total derivative
            for (auto &&g : _grad)
            {
                grad += *g;
            }
            //std::cout << "Total gradient get: " << grad << ", size: " << grad.rows() << "," << grad.cols() << std::endl;
            return grad;
        }
        else
        {
            cond_.wait(lck1, [this]() { return this->_gradientAvailable.load(); });
            //std::cout << "Notified Thread ID: " << std::this_thread::get_id() << std::endl;
            grad.setZero(_grad[0]->rows(), _grad[0]->cols());
            //  get total derivative
            for (auto &&g : _grad)
            {
                grad += *g;
            }
            //std::cout << "Total gradient get: " << grad << ", size: " << grad.rows() << "," << grad.cols() << std::endl;
            return grad;
        }
    }
    else
    {
        //return 1s if there is no consumre
        //std::cout << "No consumer" << std::endl;
        grad.setOnes(_output->rows(), _output->cols());
        return grad;
    }
}

template <typename T>
void Node<T>::setValue(T &&t)
{
    std::lock_guard<std::mutex> lck(nodeMtx_);
    // lock to avoid data race
    _dataAvailable.store(true);
    _output.reset(new T(t));
    //std::cout << "Output set: " << *_output << ", size: " << (*_output).rows() << "," << (*_output).cols() << std::endl;
}

template <typename T>
void Node<T>::setGrad(T t)
{
    // lock for all _grad and _output direct read and write
    std::lock_guard<std::mutex> lck1(nodeMtx_);
    //std::cout << "Gradient set: " << t << ", size: " << t.rows() << "," << t.cols() << std::endl;
    // gradient and value must have same dimensions
    if (t.cols() != _output->cols() or t.rows() != _output->rows())
    {
        std::cout << "Gradient and output have different dimensions!" << std::endl;
    }
    _grad.push_back(Locking_shared_ptr<T>((new T(t)), &(this->Mtx_)));
    // get the number of consumer of the node; consumerSize_ is atomic
    int cnsSize = this->consumerSize_.load();
    // check if gradient of all consumers are set; use >= as a node might not have a consumer
    if (_grad.size() >= cnsSize)
    {
        // set flag to true
        _gradientAvailable.store(true);
        // notify all threads waiting for this data
        cond_.notify_all();
    }
}

template <typename T>
void Node<T>::clearGrads()
{
    // lock for all _grad read and write
    std::lock_guard<std::mutex> lck1(nodeMtx_);
    _gradientAvailable.store(false);
    // reset gradients
    _grad.clear();
}

// --- Variable ---

template <typename T>
Variable<T>::Variable(T &&a)
{
    std::cout << "Variable contructor ..." << std::endl;
    this->_nType = nodeType::variable;
    this->_opType = operationType::NA;
    // set value locks the node, no need to create a lock
    this->setValue(std::move(a));
}

template <typename T>
Variable<T>::Variable(Variable<T> &&other)
{
    std::cout << "Variable move contructor ..." << std::endl;
    // move
    this->_nType = nodeType::variable;
    this->_opType = operationType::NA;
    // set value and get value locks the node, no need to create a lock
    T val = (&other)->getValue();
    this->setValue(std::move(*val));
}

template <typename T>
Variable<T> &Variable<T>::operator=(Variable<T> &&other)
{
    std::cout << "Variable move assignment contructor ..." << std::endl;
    if (this != &other)
    {
        // move
        this->_nType = nodeType::variable;
        this->_opType = operationType::NA;
        // set value and get value locks the node, no need to create a lock
        T val = (&other)->getValue();
        this->setValue(*val);
    }
    return *this;
}

template <typename T>
void Variable<T>::compute()
{
    return;
}

template <typename T>
void Variable<T>::gradient()
{
    //std::cout << "Variable gradient ..." << std::endl;
}

template <typename T>
void Variable<T>::updateValue(float lr)
{
    //std::cout << "Variable update value ..." << std::endl;
    // variable has only one input gradient
    // grad and output are local variables; no need for a lock
    // output is pointer but wraped aroud a locking class
    T grad = this->getGradient();
    T output = this->getValue();
    // update variable values based on learning rate and gradient
    // setValue locks nodeMtx_
    this->setValue(output.array() - (grad.array() * lr));
}

// --- Placeholder ---

template <typename T>
Placeholder<T>::Placeholder(std::string n)
{
    this->_nType = nodeType::placeholder;
    this->setName(n);
}

template <typename T>
void Placeholder<T>::compute()
{
    return;
}

template <typename T>
void Placeholder<T>::gradient()
{
    return;
}