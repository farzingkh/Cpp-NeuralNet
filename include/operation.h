#ifndef OPERATION_H
#define OPERATION_H

//#include "node.h"

// Class for operations with data value of type T
template <typename T>
class Operation : public Node<T>
{
public:
    Operation();
    virtual void compute() = 0;
};

// Operations

// addition operation with T return type value, T1 and T2 input type value
template <typename T>
class add : public Operation<T>
{
public:
    add(BaseNode &a, BaseNode &b);
    add(Variable<T> &&a, Variable<T> &&b);

    void compute();
};

// negative operation
template <typename T>
class negative : public Operation<T>
{
public:
    negative(BaseNode &a);
    void compute();
};

#include "../src/operation.tpp"

#endif /* OPERATION_H */