//
//  steady_vector.h
//  steady
//
//  Created by Marcus Zetterquist on 2013-11-13.
//  Copyright (c) 2013 Marcus Zetterquist. All rights reserved.
//

#ifndef __steady__steady_vector__
#define __steady__steady_vector__

#include <cstddef>
#include <initializer_list>
#include <vector>
#include "cpp_extension.h"

template <class T>
struct array {
};



/**
Persistent
Templetized
UT
Ranges, not iterators
Strong exception safety
Thread safe

Hash-consing for global deduplication
*/

static const size_t kBranchingFactor = 4;
static const size_t kBranchingFactorMask = 0x3;
static const size_t kBranchingFactorShift = 2;
static const size_t kBranchingFactorAntiMask = 0xffffffff - kBranchingFactorMask;


////////////////////////////////////////////		Leaf



//	1 -> kBranchingFactor values.
template <class T>
struct Leaf {
	public: Leaf();
	public: bool check_invariant() const {
		ASSERT(_rc >= 0);
		ASSERT(_rc < 1000);
		ASSERT(_values.size() <= kBranchingFactor);
		return true;
	}


	//////////////////////////////	State

	public: int32_t _rc;
	public: std::vector<T> _values;
};

template <class T>
Leaf<T>::Leaf() :
	_rc(0)
{
	ASSERT(check_invariant());
}



////////////////////////////////////////////		INode


//	An INode points to up to 32 other iNodes alternatively 32 leaf nodes.
template <class T>
struct INode {
	public: INode();
	public: bool check_invariant() const {
		ASSERT(_rc >= 0);
		ASSERT(_rc < 1000);

		//	_inodes OR _leafs is used.
		ASSERT(_inodes.empty() || _leafs.empty());
		ASSERT(_inodes.size() <= kBranchingFactor);
		ASSERT(_leafs.size() <= kBranchingFactor);
		return true;
	}

	public: int32_t _rc;

	public: std::vector<INode<T>*> _inodes;
	public: std::vector<Leaf<T>*> _leafs;
};



////////////////////////////////////////////		INode



template <class T>
INode<T>::INode() :
	_rc(0)
{
	ASSERT(check_invariant());
}



template <typename T>
struct NodeRef {
	public: NodeRef() :
		_inode(nullptr),
		_leaf(nullptr)
	{
		ASSERT(check_invariant());
	}

	public: NodeRef(INode<T>* node) :
		_inode(nullptr),
		_leaf(nullptr)
	{
		ASSERT(node != nullptr);
		ASSERT(node->check_invariant());

		_inode = node;
		_inode->_rc++;

		ASSERT(check_invariant());
	}

	public: NodeRef(Leaf<T>* node) :
		_inode(nullptr),
		_leaf(nullptr)
	{
		ASSERT(node != nullptr);
		ASSERT(node->check_invariant());

		_leaf = node;
		_leaf->_rc++;

		ASSERT(check_invariant());
	}

	public: NodeRef(const NodeRef<T>& ref) :
		_inode(nullptr),
		_leaf(nullptr)
	{
		ASSERT(ref.check_invariant());

		if(ref.GetType() == kNull){
		}
		else if(ref.GetType() == kInode){
			_inode = ref._inode;
			_inode->_rc++;
		}
		else if(ref.GetType() == kLeaf){
			_leaf = ref._leaf;
			_leaf->_rc++;
		}
		else{
			ASSERT(false);
		}

		ASSERT(check_invariant());
	}

	public: ~NodeRef(){
		ASSERT(check_invariant());

		if(GetType() == kNull){
		}
		else if(GetType() == kInode){
			_inode->_rc--;
			if(_inode->_rc == 0){
				delete _inode;
				_inode = nullptr;
			}
		}
		else if(GetType() == kLeaf){
			_leaf->_rc--;
			if(_leaf->_rc == 0){
				delete _leaf;
				_leaf = nullptr;
			}
		}
		else{
			ASSERT(false);
		}
	}

	public: bool check_invariant() const {
		ASSERT(_inode == nullptr || _leaf == nullptr);

		return true;
	}


	public: void swap(NodeRef<T>& other){
		ASSERT(check_invariant());
		ASSERT(other.check_invariant());

		std::swap(_inode, other._inode);
		std::swap(_leaf, other._leaf);

		ASSERT(check_invariant());
		ASSERT(other.check_invariant());
	}


	public: NodeRef<T>& operator=(const NodeRef<T>& other){
		ASSERT(check_invariant());
		ASSERT(other.check_invariant());

		NodeRef<T> temp(other);

		temp.swap(*this);

		ASSERT(check_invariant());
		ASSERT(other.check_invariant());
		return *this;
	}
	



	///////////////////////////////////////		Internals

	public: enum NodeType {
		kNull = 4,
		kInode,
		kLeaf
	};

	public: NodeType GetType() const {
		if(_inode == nullptr && _leaf == nullptr){
			return kNull;
		}
		else if(_inode != nullptr){
			return kInode;
		}
		else if(_leaf != nullptr){
			return kLeaf;
		}
		else{
			ASSERT(false);
		}
	}

	INode<T>* _inode;
	Leaf<T>* _leaf;

};



////////////////////////////////////////////		steady_vector



template <class T>
class steady_vector {
	public: steady_vector();
	public: steady_vector(const std::vector<T>& vec);
	public: steady_vector(const T entries[], size_t count);
	public: steady_vector(std::initializer_list<T> args);

	//public: steady_vector(T entries[], std::size_t count);
	public: ~steady_vector();
	public: bool check_invariant() const;

	public: steady_vector(const steady_vector& rhs);
	public: steady_vector& operator=(const steady_vector& rhs);
	public: void swap(steady_vector& other);

	// ###	operator== and !=

	public: steady_vector push_back(const T& v) const;
//	public: steady_vector update(size_t index, const T& v) const;
	public: std::size_t size() const;
	public: bool empty() const{
		return size() == 0;
	}

	public: T operator[](const std::size_t index) const{
		return get_at(index);
	}

	public: T get_at(const std::size_t index) const;

	public: std::vector<T> to_vec() const;



	///////////////////////////////////////		Internals

	public: steady_vector(NodeRef<T> root, std::size_t size);

	private: NodeRef<T> insert(NodeRef<T> node, size_t size, size_t pos, const T& v) const;


	///////////////////////////////////////		State
		public: NodeRef<T> _root;
//		private: T* _allocation;
		public: std::size_t _size;
};

	
#endif /* defined(__steady__steady_vector__) */
