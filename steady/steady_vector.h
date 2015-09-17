
#ifndef __steady__vector__
#define __steady__vector__

#include "quark.h"
#include <initializer_list>
#include <atomic>
#include <vector>
#include <array>


/*
	Persistent vector class, steady::vector<T>

	Vector object is immutable = can never be changed. This makes for robust code and thread safety etc.

	When you modify the vector you always get a copy of the vector with your changes integrated.

	Internally the new and old vectors shares most state so this is fast and uses little memory.


	Based on Clojure's vector.

	NOTICE: if T has member functions that throws exception, so will vector.


	Some member functions have naive implementation that are very slow right now.
*/

/*
			//	Give QUARK macros shorter names
			#define ASSERT(x) QUARK_ASSERT(x)
			#define TRACE(x) QUARK_TRACE(x)
			#define TRACE_SS(x) QUARK_TRACE_SS(x)
			#define VERIFY(x) QUARK_TEST_VERIFY(x)
*/

#define STEADY_ASSERT(x) QUARK_ASSERT(x)
#define STEADY_TRACE(x) QUARK_TRACE(x)
#define STEADY_TRACE_SS(x) QUARK_TRACE_SS(x)
#define STEADY_TEST_VERIFY(x) QUARK_TEST_VERIFY(x)

#define STEADY_SCOPED_TRACE(x) QUARK_SCOPED_TRACE(x)
#define STEADY_ASSERT_UNREACHABLE QUARK_ASSERT_UNREACHABLE




namespace steady {

	//	Change BRANCHING_FACTOR_SHIFT to get different branching factors. Number of bits per inode.
	//	5 ( = 32 values per node) is ideal.
	static const int BRANCHING_FACTOR_SHIFT = 5;
	static const int BRANCHING_FACTOR = 1 << BRANCHING_FACTOR_SHIFT;



	namespace internals {
		template <typename T> struct node_ref;
		template <typename T> struct inode;
		template <typename T> struct leaf_node;

		static const size_t BRANCHING_FACTOR_MASK = (BRANCHING_FACTOR - 1);

		static const int EMPTY_TREE_SHIFT = -BRANCHING_FACTOR_SHIFT;
		static const int LEAF_NODE_SHIFT = 0;
		static const int LOWEST_LEVEL_INODE_SHIFT = BRANCHING_FACTOR_SHIFT;


		////////////////////////////////////////////		node_type

		enum class node_type {
			null_node = 4,
			inode,
			leaf_node
		};


		////////////////////////////////////////////		Utility functions

		size_t divide_round_up(size_t value, size_t align);


		/*
			Returns how deep node hiearchy is for a tree with *count* values. Counts both leaf-nodes and inodes.
			0: empty
			1: one leaf node.
			2: one inode with 1-4 leaf nodes.
			3: two levels of inodes plus leaf nodes.
		*/
		inline int count_to_depth(size_t count);

		/*
			Given a shift value, how many values can this tree hold without introduce more levels of inodes?
		*/
		size_t shift_to_max_size(int shift);


		/*
			Return how many steps to shift vector-index to get its *top-level* bits.

			-BRANCHING_FACTOR_SHIFT: empty tree
			0: leaf-node level
			BRANCHING_FACTOR_SHIFT: inode1 (inode that points to leafnodes)
			>=BRANCHING_FACTOR_SHIFT: inode that points to inodes.
		*/
		int vector_size_to_shift(size_t size);



		////////////////////////////////////////////		leaf_node

		/*
			This object holds a number of values, of type T.
			These nodes live at the bottom of an inode tree.

			Holds an intrusive reference counter that is used by client code.
		*/

		template <class T>
		struct leaf_node {
			public: leaf_node() :
				_rc(0)
			{
				_debug_count++;
				STEADY_ASSERT(check_invariant());
			}

			public: leaf_node(const std::array<T, BRANCHING_FACTOR>& values) :
				_rc(0),
				_values(values)
			{
				_debug_count++;
				STEADY_ASSERT(check_invariant());
			}

			public: ~leaf_node(){
				STEADY_ASSERT(check_invariant());
				STEADY_ASSERT(_rc == 0);

				_debug_count--;
			}

			public: bool check_invariant() const {
				STEADY_ASSERT(_rc >= 0);
				STEADY_ASSERT(_rc < 1000);
				STEADY_ASSERT(_values.size() == BRANCHING_FACTOR);
				return true;
			}

			private: leaf_node<T>& operator=(const leaf_node& rhs);
			private: leaf_node(const leaf_node& rhs);



			//////////////////////////////	State

			public: std::atomic<int32_t> _rc;
			public: std::array<T, BRANCHING_FACTOR> _values{};
			public: static int _debug_count;
		};

		template <class T>
		int leaf_node<T>::_debug_count = 0;



		////////////////////////////////////////////		inode

		/*
			An inode is used to hold leaf nodes when more than one leaf node is needed. It can also hold other inodes.

			An inode has these states:
				full set of iNode pointers or
				full set of leaf node pointers or
				empty vector.

			You cannot mix sub-inode and sub-leaf nodes in the same inode.
			inode pointers and leaf node pointers can be null, but the nulls are always at the end of the arrays.

			Holds an intrusive reference counter that is used by client code.
		*/

		template <class T>
		struct inode {
			public: typedef std::array<node_ref<T>, BRANCHING_FACTOR> children_t;

			//	children: 0-32 children, all of the same type. kNullNodes can only appear at end of vector.
			public: inode(const children_t& children2) :
				_rc(0),
				_children(children2)
			{
				STEADY_ASSERT(children2.size() >= 0);
				STEADY_ASSERT(children2.size() <= BRANCHING_FACTOR);
		#if QUARK__ASSERT_ON
				for(auto i: children2){
					i.check_invariant();
				}
		#endif

				_debug_count++;
				STEADY_ASSERT(check_invariant());
			}


			public: ~inode(){
				STEADY_ASSERT(check_invariant());
				STEADY_ASSERT(_rc == 0);

				_debug_count--;
			}

			private: inode<T>& operator=(const inode& rhs);
			private: inode(const inode& rhs);

			public: bool check_invariant() const {
				STEADY_ASSERT(_rc >= 0);
				STEADY_ASSERT(_rc < 10000);
				STEADY_ASSERT(validate_inode_children(_children));

				return true;
			}

			//	Counts the children actually used = skips trailing any null children.
			public: size_t count_children() const{
				STEADY_ASSERT(check_invariant());

				size_t index = 0;
				while(index < _children.size() && _children[index].get_type() != node_type::null_node){
					index++;
				}
				return index;
			}

			//	Returns entire array, even if not all items are used.
			public: children_t get_child_array() const{
				STEADY_ASSERT(check_invariant());

				return _children;
			}

			public: node_ref<T> get_child(size_t index) const{
				STEADY_ASSERT(check_invariant());
				STEADY_ASSERT(index < BRANCHING_FACTOR);
				STEADY_ASSERT(index < _children.size());

				return _children[index];
			}

			//	You can only call this for leaf nodes.
			public: const leaf_node<T>* get_child_as_leaf_node(size_t index) const{
				STEADY_ASSERT(check_invariant());
				STEADY_ASSERT(index < _children.size());

				STEADY_ASSERT(_children[0].get_type() == node_type::leaf_node);
				return _children[index]._leaf_node;
			}


			//////////////////////////////	State

			public: std::atomic<int32_t> _rc;
			private: children_t _children;
			public: static int _debug_count;
		};

		template <class T>
		int inode<T>::_debug_count = 0;




		////////////////////////////////////////////		node_ref<T>

		template <typename T>
		struct node_ref {
			public: node_ref();

			public: node_ref(inode<T>* node);
			public: node_ref(leaf_node<T>* node);

			public: node_ref(const node_ref<T>& ref);

			public: ~node_ref();

			public: bool check_invariant() const;
			public: void swap(node_ref<T>& rhs);
			public: node_ref<T>& operator=(const node_ref<T>& rhs);
			
			public: node_type get_type() const;
			public: const inode<T>* get_inode() const;
			public: const leaf_node<T>* get_leaf_node() const;
			public: leaf_node<T>* get_leaf_node();

			///////////////////////////////////////		State

			private: inode<T>* _inode;
			public: leaf_node<T>* _leaf_node;
		};

	}	//	Internals




////////////////////////////////////////////		vector

/*
	Persistent vector class.
*/

template <class T>
class vector {

	/*
		Makes empty vector.
		No memory allocation.
		O(1)
	*/
	public: vector();

	/*
		Makes vector containing values from a std::vector<>.
		Allocates memory.

		values: zero -> many values.
		this: on exit this holds the new vector
	*/
	public: vector(const std::vector<T>& values);

	/*
		Makes vector containing _count_ values from _values_.
		Values are copied into the vector.
		Allocates memory.

		values: must not be nullptr, not even when count == 0
		count: >= 0
		this: on exit this holds the new vector
	*/
	public: vector(const T values[], size_t count);

	/*
		C++11 initializer-list constructor. Allows you to write
			const vector<int>({ 1, 2, 3 });

		Allocates memory.

		args: a C++11 initializer list object that specifies the values for the new vector
		this: on exit this holds the new vector
	*/
	public: vector(std::initializer_list<T> args);

	/*
		no-throw
	*/
	public: ~vector();

	/*
		Development feature: validates the internal state of the vector and calls ASSERT on defects.
		use like:
			STEADY_ASSERT(myVector.check_invariant());

		this: on exit, this will hold a copy of _rhs_
		return: true = internal state is correct, false if not.
	*/
	public: bool check_invariant() const;

	/*
		Copies a vector object.
		Extremely fast since it shares the entire state with rhs, just updated the reference counter.
		No memory allocation.
		O(1)

		this: on exit, this will hold a copy of _rhs_
		rhs: vector to copy.
	*/
	public: vector(const vector& rhs);

	/*
		Same as copy-constructor.
		Your existing variable holding the vector will be changd to hold vector rhs instead.
		Notice: this function may appear to mutate the instance, but it doesn't.
		No memory allocation.
		O(1)

		this: on entry this is the destination vector, on exit, it will hold _rhs_.
		rhs: source vector
	*/
	public: vector& operator=(const vector& rhs);

	/*
		The variable holding your vector will be changed to hold the vector specified by _rhs_ and vice versa.
		The vector objects are not mutated, they just switch place.
		O(1)
		No-throw guarantee.

		this: on entry this holds vector A, on exit it holds vector B
		rhs: on entry this holds vector B, on exit it holds vector A
	*/
	public: void swap(vector& rhs);

	/*
		Store value into the vector
		Old vector will not be changed, instead a new vector with the store will be returned.
		The new and old vector share most internal state.
		O(1) ... almost

		this: input vector
		index: 0 -> (size - 1)
		value: new value to store
		return: new copy of the vector with _value_ stored at the _index_.
	*/
	public: vector store(size_t index, const T& value) const;

	/*
		Append value to the end of the vector, returning a vector with size + 1.
		Old vector will not be changed, instead a new vector with the store will be returned.
		The new and old vector share most internal state.
		O(1) ... almost

		value: value to append.
		return: new copy of the vector, with _value_ tacked to the end. It will be 1 bigger than the input vector.
	*/
	public: vector push_back(const T& value) const;

	/*
		Remove last value in the vector, returning a vector with size - 1.

		this: must have size() > 0
		return: new copy of the vector, with the last values removed. It will be 1 smaller than input vector.
	*/
	public: vector pop_back() const;

	/*
		Returns true if vectors are equivalent.

		Worst case is O(n) but performance is better when sharing is detected between vectors.
		Best case: O(1)
		No memory allocation.

		return: true if vectors have the same size and operator==() on every value are true
	*/
	public: bool operator==(const vector& rhs) const;
	public: bool operator!=(const vector& rhs) const{
		return !(*this == rhs);
	}

	/*
		How many values does the vector hold?

		this: vector
		return: the number of values in the vector. 0 ->
	*/
	public: std::size_t size() const;

	public: bool empty() const{
		return size() == 0;
	}

	/*
		Get value at index
		O(1) ... almost

		this: input vector
		index: 0 -> (size - 1)
		return: value at index _index_.
	*/
	public: T operator[](const std::size_t index) const;

	public: std::vector<T> to_vec() const;



	///////////////////////////////////////		Internals

	/*
		Traces a diagram of the structure of the vector using TRACE.
	*/
	public: void trace_internals() const;

	public: const internals::node_ref<T>& get_root() const{
		return _root;
	}
	public: vector(internals::node_ref<T> root, std::size_t size, int shift);

	public: int get_shift() const;


	//	Efficient way to access values. Multiple of BRANCHING_FACTOR.
	public: size_t get_block_count() const;

	/*
		Last block may be partial if vector isn't multiple of block size.
		You can only call this function when get_block_count() returns > 0.
	*/
	public: const T* get_block(size_t block_index) const;


	///////////////////////////////////////		State

	private: internals::node_ref<T> _root;
	private: std::size_t _size = 0;

	//	This is the number of shift-steps needed to get to root.
	//	It can be calculated from _size but that is slow so we cache it.
	private: int _shift = internals::EMPTY_TREE_SHIFT;
};



////////////////////////////////////////////		Global functions

/*
	Appends two vectors and returns a new one.
*/
template <class T>
vector<T> operator+(const vector<T>& a, const vector<T>& b);








////////////////////////////////////////////		IMPLEMENTATION

	namespace internals {


		/*
			Traces simple graph of the nodes in the tree.
		*/
		template <class T>
		void trace_node(const std::string& prefix, const internals::node_ref<T>& node){
			if(node.get_type() == internals::node_type::null_node){
				STEADY_TRACE_SS(prefix << "<null>");
			}
			else if(node.get_type() == internals::node_type::inode){
				std::stringstream s;
				s << prefix << "<inode> RC: " << node.get_inode()->_rc;
				STEADY_SCOPED_TRACE(s.str());

				int index = 0;
				for(auto i: node.get_inode()->get_child_array()){
					trace_node("#" + std::to_string(index) + "\t", i);
					index++;
				}
			}
			else if(node.get_type() == internals::node_type::leaf_node){
				std::stringstream s;
				s << prefix << "<leaf> RC: " << node.get_leaf_node()->_rc;
				STEADY_SCOPED_TRACE(s.str());

				int index = 0;
				for(auto i: node.get_leaf_node()->_values){
					STEADY_TRACE_SS("#" << std::to_string(index) << "\t" << i);
					(void)i;
					index++;
				}
			}
			else{
				STEADY_ASSERT_UNREACHABLE;
			}
		}

		/*
			Validates the list, not that the children är valid.
		*/
		template <class T>
		bool validate_inode_children(const std::array<node_ref<T>, BRANCHING_FACTOR>& vec){
			STEADY_ASSERT(vec.size() >= 0);
			STEADY_ASSERT(vec.size() <= BRANCHING_FACTOR);

			/*
			for(auto i: vec){
				i.check_invariant();
			}
			*/

			if(vec.size() > 0){
				const auto type = vec[0].get_type();
				if(type == node_type::null_node){
					for(auto i: vec){
						STEADY_ASSERT(i.get_type() == node_type::null_node);
					}
				}
				else if(type == node_type::inode){
					int i = 0;
					while(i < vec.size() && vec[i].get_type() == node_type::inode){
						i++;
					}
					while(i < vec.size()){
						STEADY_ASSERT(vec[i].get_type() == node_type::null_node);
						i++;
					}
				}
				else if(type == node_type::leaf_node){
					int i = 0;
					while(i < vec.size() && vec[i].get_type() == node_type::leaf_node){
						i++;
					}
					while(i < vec.size()){
						STEADY_ASSERT(vec[i].get_type() == node_type::null_node);
						i++;
					}
				}
				else{
					STEADY_ASSERT(false);
				}
			}
			return true;
		}


		template <class T>
		node_ref<T> make_leaf_node(const std::array<T, BRANCHING_FACTOR>& values){
			return node_ref<T>(new leaf_node<T>(values));
		}


		template <class T>
		node_ref<T> make_inode_from_vector(const std::vector<node_ref<T>>& children){
			STEADY_ASSERT(children.size() >= 0);
			STEADY_ASSERT(children.size() <= BRANCHING_FACTOR);

			std::array<node_ref<T>, BRANCHING_FACTOR> temp{};
			std::copy(children.begin(), children.end(), temp.begin());
			return node_ref<T>(new inode<T>(temp));
		}


		template <class T>
		node_ref<T> make_inode_from_array(const std::array<node_ref<T>, BRANCHING_FACTOR>& children){
			return node_ref<T>(new inode<T>(children));
		}


		/*
			Verifies the tree is valid.
			### improve
		*/
		template <class T>
		bool tree_check_invariant(const node_ref<T>& tree, size_t size){
			STEADY_ASSERT(tree.check_invariant());
		#if QUARK__ASSERT_ON
			if(size == 0){
				STEADY_ASSERT(tree.get_type() == node_type::null_node);
			}
			else{
				STEADY_ASSERT(tree.get_type() != node_type::null_node);
			}
		#endif
			return true;
		}


		template <class T>
		node_ref<T> find_leaf_node(const vector<T>& original, size_t index){
			STEADY_ASSERT(original.check_invariant());
			STEADY_ASSERT(index < original.size());

			auto shift = original.get_shift();
			node_ref<T> node_it = original.get_root();

			//	Traverse all inodes.
			while(shift > 0){
				size_t slot_index = (index >> shift) & BRANCHING_FACTOR_MASK;
				node_it = node_it.get_inode()->get_child(slot_index);
				shift -= BRANCHING_FACTOR_SHIFT;
			}

			STEADY_ASSERT(shift == LEAF_NODE_SHIFT);
			STEADY_ASSERT(node_it.get_type() == node_type::leaf_node);
			return node_it;
		}


		/*
			node: original tree. Not changed by function. Cannot be null node, only inode or leaf node.

			shift: shift for current level in tree.
			leaf_index0: index of the first value in the leaf.
			new_leaf: new leaf node.
			result: copy of "tree" that has new_leaf replaced. Same size as original.
				result-tree and original tree shares internal state.
		*/
		template <class T>
		node_ref<T> replace_leaf_node(const node_ref<T>& node, int shift, size_t leaf_index0, const node_ref<T>& new_leaf){
			STEADY_ASSERT(node.get_type() == node_type::inode || node.get_type() == node_type::leaf_node);
			STEADY_ASSERT(new_leaf.check_invariant());

			const size_t slot_index = (leaf_index0 >> shift) & BRANCHING_FACTOR_MASK;

			if(shift == LEAF_NODE_SHIFT){
				STEADY_ASSERT(node.get_type() == node_type::leaf_node);

				return new_leaf;
			}
			else{
				STEADY_ASSERT(node.get_type() == node_type::inode);

				const auto child = node.get_inode()->get_child(slot_index);
				auto child2 = replace_leaf_node(child, shift - BRANCHING_FACTOR_SHIFT, leaf_index0, new_leaf);

				auto children = node.get_inode()->get_child_array();
				children[slot_index] = child2;
				auto copy = make_inode_from_array(children);
				return copy;
			}
		}


		/*
			Recursively finds the correct leaf node and replaces the value _value_ with it. Returns new tree.

			node: original tree. Not changed by function. Cannot be null node, only inode or leaf node.
			shift: shift for current level in tree.
			index: entry to store "value" to.
			value: value to store.
			result: copy of "tree" that has "value" stored. Same size as original.
				result-tree and original tree shares internal state.
		*/
		template <class T>
		node_ref<T> replace_value(const node_ref<T>& node, int shift, size_t index, const T& value){
			STEADY_ASSERT(node.get_type() == node_type::inode || node.get_type() == node_type::leaf_node);

			const size_t slot_index = (index >> shift) & BRANCHING_FACTOR_MASK;
			if(shift == LEAF_NODE_SHIFT){
				STEADY_ASSERT(node.get_type() == node_type::leaf_node);

				auto copy = node_ref<T>(new leaf_node<T>(node.get_leaf_node()->_values));

				STEADY_ASSERT(slot_index < copy.get_leaf_node()->_values.size());
				copy.get_leaf_node()->_values[slot_index] = value;

				return copy;
			}
			else{
				STEADY_ASSERT(node.get_type() == node_type::inode);

				const auto child = node.get_inode()->get_child(slot_index);
				auto child2 = replace_value(child, shift - BRANCHING_FACTOR_SHIFT, index, value);

				auto children = node.get_inode()->get_child_array();
				children[slot_index] = child2;
				auto copy = make_inode_from_array(children);
				return copy;
			}
		}

		/*
			Creates a leaf node with zero to many parent inodes (all inodes only contain one item).

			Example path:
				inode
					inode
						inode
							leaf_node

			Example path:
				inode
					leaf_node

			Example path:
				leaf_node
		*/
		template <class T>
		node_ref<T> make_new_path(int shift, const node_ref<T>& leaf_node){
			STEADY_ASSERT(leaf_node.check_invariant());
			STEADY_ASSERT(leaf_node.get_type() == node_type::leaf_node);

			if(shift == LEAF_NODE_SHIFT){
				return leaf_node;
			}
			else{
				auto a = make_new_path(shift - BRANCHING_FACTOR_SHIFT, leaf_node);
				node_ref<T> b = make_inode_from_array<T>({ a });
				return b;
			}
		}

		/*
			Adds the new leaf-node last in the tree. Returns new tree.

			original: original tree. Not changed by function. Cannot be null node, only inode or leaf node.
			index: point to location of new leaf-mode = current number of values in tree.
			value: value to store.
			result: copy of "tree" that has "value" stored. Same size as original.
				result-tree and original tree shares internal state

			New tree may be same depth or +1 deep.

			Cannot append node when root gets full!
		*/
		template <class T>
		node_ref<T> append_leaf_node(const node_ref<T>& original, int shift, size_t index, const node_ref<T>& leaf_node){
			STEADY_ASSERT(original.check_invariant());
			STEADY_ASSERT(original.get_type() == node_type::inode);
			STEADY_ASSERT(leaf_node.check_invariant());
			STEADY_ASSERT(leaf_node.get_type() == node_type::leaf_node);

			size_t slot_index = (index >> shift) & BRANCHING_FACTOR_MASK;
			auto children = original.get_inode()->get_child_array();

			//	Lowest level inode, pointing to leaf nodes.
			if(shift == LOWEST_LEVEL_INODE_SHIFT){
				children[slot_index] = leaf_node;
				return make_inode_from_array<T>(children);
			}
			else {
				const auto child = children[slot_index];
				if(child.get_type() == node_type::null_node){
					node_ref<T> child2 = make_new_path(shift - BRANCHING_FACTOR_SHIFT, leaf_node);
					children[slot_index] = child2;
					return make_inode_from_array<T>(children);
				}
				else{
					node_ref<T> child2 = append_leaf_node(child, shift - BRANCHING_FACTOR_SHIFT, index, leaf_node);
					children[slot_index] = child2;
					return make_inode_from_array<T>(children);
				}
			}
		}


		/*
			Original must be a multiple of BRANCHING_FACTOR - no partial leaf node.
		*/
		template <class T>
		vector<T> push_back_leaf_node(const vector<T>& original, const node_ref<T>& new_leaf, size_t leaf_item_count){
			STEADY_ASSERT(original.check_invariant());
			STEADY_ASSERT(new_leaf.check_invariant());
			STEADY_ASSERT(new_leaf.get_type() == node_type::leaf_node);
			STEADY_ASSERT((original.size() & BRANCHING_FACTOR_MASK) == 0);
			STEADY_ASSERT(leaf_item_count <= BRANCHING_FACTOR);

			const auto original_size = original.size();
			const auto original_shift = original.get_shift();

			if(original_size == 0){
				const auto result = vector<T>(new_leaf, leaf_item_count, LEAF_NODE_SHIFT);
				STEADY_ASSERT(result.check_invariant());
				return result;
			}
			else{
				//	How many values can we fit in tree with this shift-constant?
				size_t max_values = internals::shift_to_max_size(original_shift);
				bool fits_in_root = (original_size + leaf_item_count) <= max_values;

				//	Space left in root?
				if(fits_in_root){
					const auto root = append_leaf_node(original.get_root(), original_shift, original_size, new_leaf);
					const auto result = vector<T>(root, original_size + leaf_item_count, original_shift);
					STEADY_ASSERT(result.check_invariant());
					return result;
				}
				else{
					auto new_path = make_new_path(original_shift, new_leaf);
					auto new_root = make_inode_from_array<T>({ original.get_root(), new_path });
					const auto result = vector<T>(new_root, original_size + leaf_item_count, original_shift + BRANCHING_FACTOR_SHIFT);
					STEADY_ASSERT(result.check_invariant());
					return result;
				}
			}
		}


		template <class T>
		vector<T> push_back_1(const vector<T>& original, const T& value){
			STEADY_ASSERT(original.check_invariant());

			const auto size = original.size();

			//	Does last leaf node have space for one more value? Then we use replace_value() - keeping tree same size.
			if((size & BRANCHING_FACTOR_MASK) != 0){
				const auto shift = original.get_shift();
				const auto root = replace_value(original.get_root(), shift, size, value);
				return vector<T>(root, size + 1, shift);
			}
			else{
				const auto leaf = make_leaf_node<T>({ value });
				return push_back_leaf_node(original, leaf, 1);
			}
		}


		/*
			This is the central building block: adds many values to a vector (or a create a new vector) fast.
		*/
#if false
		template <class T>
		vector<T> push_back_batch(const vector<T>& original, const T values[], size_t count){
			STEADY_ASSERT(original.check_invariant());
			STEADY_ASSERT(values != nullptr);

			vector<T> result = original;
			for(size_t i = 0 ; i < count ; i++){
				result = result.push_back(values[i]);
			}
			return result;
		}

#else

		template <class T>
		vector<T> push_back_batch(const vector<T>& original, const T values[], size_t count){
			STEADY_ASSERT(original.check_invariant());
			STEADY_ASSERT(values != nullptr);

			vector<T> result = original;
			size_t source_pos = 0;

			/*
				1) If the last leaf node in destination is partially filled, pad it out.
			*/
			{
				size_t last_leaf_size = original.size() & BRANCHING_FACTOR_MASK;
				if(last_leaf_size > 0){
					size_t last_leaf_node_index = original.size() & ~(BRANCHING_FACTOR_MASK);
#if false
					size_t copy_count = std::min(BRANCHING_FACTOR - last_leaf_size, count);
					for(size_t i = 0 ; i < copy_count ; i++){
						result = push_back_1(result, values[source_pos]);
						source_pos++;
					}
#else
					size_t copy_count = std::min(BRANCHING_FACTOR - last_leaf_size, count);
					node_ref<T> prev_leaf = find_leaf_node(result, last_leaf_node_index);
					node_ref<T> new_leaf_node(new leaf_node<T>());

					//	Copy existing values.
					std::copy(&prev_leaf.get_leaf_node()->_values[0],
						&prev_leaf.get_leaf_node()->_values[last_leaf_size],
						new_leaf_node.get_leaf_node()->_values.begin());

					//	Append our new values.
					std::copy(&values[source_pos],
						&values[source_pos + copy_count],
						new_leaf_node.get_leaf_node()->_values.begin() + last_leaf_size);

					node_ref<T> new_root = replace_leaf_node(result.get_root(), result.get_shift(), last_leaf_node_index, new_leaf_node);
					result = vector<T>(new_root, result.size() + copy_count, result.get_shift());
					source_pos += copy_count;

#endif
				}
			}

			/*
				Append _entire leaf nodes_ while there are enough source values. Includes appending a last, partial leaf.
			*/
			if(source_pos < count){
				while(source_pos < count){
					STEADY_ASSERT((result.size() & BRANCHING_FACTOR_MASK) == 0);

					auto new_leaf_node = node_ref<T>(new leaf_node<T>());
					size_t batch_count = std::min(count - source_pos, static_cast<size_t>(BRANCHING_FACTOR));

					std::copy(&values[source_pos],
						&values[source_pos + batch_count],
						new_leaf_node.get_leaf_node()->_values.begin());

					result = internals::push_back_leaf_node(result, new_leaf_node, batch_count);

					source_pos += batch_count;
				}
			}

			STEADY_ASSERT(result.check_invariant());
			STEADY_ASSERT(result.size() == original.size() + count);
			return result;
		}
#endif




		////////////////////////////////////////////		node_ref<T>

		/*
			Safe, reference counted handle that holds either an inode, a LeadNode or null.
		*/

		template <typename T>
		node_ref<T>::node_ref() :
			_inode(nullptr),
			_leaf_node(nullptr)
		{
			STEADY_ASSERT(check_invariant());
		}

		/*
			Will assume ownership of the input node - caller must not delete it after call returns.
			Adds ref.
			node == nullptr => null_node
		*/
		template <typename T>
		node_ref<T>::node_ref(inode<T>* node) :
			_inode(nullptr),
			_leaf_node(nullptr)
		{
			if(node != nullptr){
				STEADY_ASSERT(node->check_invariant());
				STEADY_ASSERT(node->_rc >= 0);

				_inode = node;
				_inode->_rc++;
			}

			STEADY_ASSERT(check_invariant());
		}

		/*
			Will assume ownership of the input node - caller must not delete it after call returns.
			Adds ref.
			node == nullptr => null_node
		*/
		template <typename T>
		node_ref<T>::node_ref(leaf_node<T>* node) :
			_inode(nullptr),
			_leaf_node(nullptr)
		{
			if(node != nullptr){
				STEADY_ASSERT(node->check_invariant());
				STEADY_ASSERT(node->_rc >= 0);

				_leaf_node = node;
				_leaf_node->_rc++;
			}

			STEADY_ASSERT(check_invariant());
		}

		//	Uses reference counting to share all state.
		template <typename T>
		node_ref<T>::node_ref(const node_ref<T>& ref) :
			_inode(nullptr),
			_leaf_node(nullptr)
		{
			STEADY_ASSERT(ref.check_invariant());

			if(ref.get_type() == node_type::null_node){
			}
			else if(ref.get_type() == node_type::inode){
				_inode = ref._inode;
				_inode->_rc++;
			}
			else if(ref.get_type() == node_type::leaf_node){
				_leaf_node = ref._leaf_node;
				_leaf_node->_rc++;
			}
			else{
				STEADY_ASSERT(false);
			}

			STEADY_ASSERT(check_invariant());
		}

		template <typename T>
		node_ref<T>::~node_ref(){
			STEADY_ASSERT(check_invariant());

			if(get_type() == node_type::null_node){
			}
			else if(get_type() == node_type::inode){
				_inode->_rc--;
				if(_inode->_rc == 0){
					delete _inode;
					_inode = nullptr;
				}
			}
			else if(get_type() == node_type::leaf_node){
				_leaf_node->_rc--;
				if(_leaf_node->_rc == 0){
					delete _leaf_node;
					_leaf_node = nullptr;
				}
			}
			else{
				STEADY_ASSERT(false);
			}
		}

		template <typename T>
		bool node_ref<T>::check_invariant() const {
			STEADY_ASSERT(_inode == nullptr || _leaf_node == nullptr);

			if(_inode != nullptr){
				STEADY_ASSERT(_inode->check_invariant());
				STEADY_ASSERT(_inode->_rc > 0);
			}
			else if(_leaf_node != nullptr){
				STEADY_ASSERT(_leaf_node->check_invariant());
				STEADY_ASSERT(_leaf_node->_rc > 0);
			}
			return true;
		}

		template <typename T>
		void node_ref<T>::swap(node_ref<T>& rhs){
			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(rhs.check_invariant());

			std::swap(_inode, rhs._inode);
			std::swap(_leaf_node, rhs._leaf_node);

			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(rhs.check_invariant());
		}

		template <typename T>
		node_ref<T>& node_ref<T>::operator=(const node_ref<T>& rhs){
			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(rhs.check_invariant());

			node_ref<T> temp(rhs);

			temp.swap(*this);

			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(rhs.check_invariant());
			return *this;
		}

		template <typename T>
		node_type node_ref<T>::get_type() const {
			STEADY_ASSERT(_inode == nullptr || _leaf_node == nullptr);

			if(_inode == nullptr && _leaf_node == nullptr){
				return node_type::null_node;
			}
			else if(_inode != nullptr){
				return node_type::inode;
			}
			else if(_leaf_node != nullptr){
				return node_type::leaf_node;
			}
			else{
				STEADY_ASSERT_UNREACHABLE;
			}
		}

		template <typename T>
		const inode<T>* node_ref<T>::get_inode() const {
			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(get_type() == node_type::inode);

			return _inode;
		}

		template <typename T>
		const leaf_node<T>* node_ref<T>::get_leaf_node() const {
			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(get_type() == node_type::leaf_node);

			return _leaf_node;
		}

		template <typename T>
		leaf_node<T>* node_ref<T>::get_leaf_node() {
			STEADY_ASSERT(check_invariant());
			STEADY_ASSERT(get_type() == node_type::leaf_node);

			return _leaf_node;
		}

	}	//	internals




/////////////////////////////////////////////			vector implementation



template <class T>
vector<T>::vector(){
	STEADY_ASSERT(check_invariant());
}


template <class T>
vector<T>::vector(const std::vector<T>& values){
	//	!!! Illegal to take adress of first element of vec if it's empty.
	if(!values.empty()){
		auto temp = internals::push_back_batch(vector<T>(), &values[0], values.size());
		temp.swap(*this);
	}

	STEADY_ASSERT(size() == values.size());
	STEADY_ASSERT(check_invariant());
}

template <class T>
vector<T>::vector(const T values[], size_t count){
	STEADY_ASSERT(values != nullptr);

	auto temp = internals::push_back_batch(vector<T>(), values, count);
	temp.swap(*this);

	STEADY_ASSERT(size() == count);
	STEADY_ASSERT(check_invariant());
}


template <class T>
vector<T>::vector(std::initializer_list<T> args){
	auto temp = internals::push_back_batch(vector<T>(), args.begin(), args.end() - args.begin());
	temp.swap(*this);

	STEADY_ASSERT(size() == args.size());
	STEADY_ASSERT(check_invariant());
}


template <class T>
vector<T>::~vector(){
	STEADY_ASSERT(check_invariant());

	_size = -1;
}


template <class T>
bool vector<T>::check_invariant() const{
	if(_root.get_type() == internals::node_type::null_node){
		STEADY_ASSERT(_size == 0);
	}
	else{
		STEADY_ASSERT(_size >= 0);
	}
	STEADY_ASSERT(tree_check_invariant(_root, _size));

	STEADY_ASSERT(_shift >= internals::EMPTY_TREE_SHIFT && _shift < 32);
	STEADY_ASSERT(_shift == internals::vector_size_to_shift(_size));

	return true;
}


template <class T>
vector<T>::vector(const vector& rhs){
	STEADY_ASSERT(rhs.check_invariant());

	internals::node_ref<T> newRef(rhs._root);

	_root = newRef;
	_size = rhs._size;
	_shift = rhs._shift;

	STEADY_ASSERT(check_invariant());
}


template <class T>
vector<T>& vector<T>::operator=(const vector& rhs){
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(rhs.check_invariant());

	vector<T> temp(rhs);
	temp.swap(*this);

	STEADY_ASSERT(check_invariant());
	return *this;
}


template <class T>
void vector<T>::swap(vector& rhs){
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(rhs.check_invariant());

	_root.swap(rhs._root);
	std::swap(_size, rhs._size);
	std::swap(_shift, rhs._shift);

	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(rhs.check_invariant());
}


template <class T>
vector<T>::vector(internals::node_ref<T> root, std::size_t size, int shift) :
	_root(root),
	_size(size),
	_shift(shift)
{
	STEADY_ASSERT(shift >= internals::EMPTY_TREE_SHIFT);
	STEADY_ASSERT(internals::vector_size_to_shift(size) == shift);
	STEADY_ASSERT(check_invariant());
}


template <class T>
int vector<T>::get_shift() const{
	STEADY_ASSERT(check_invariant());

	return _shift;
}



template <class T>
size_t vector<T>::get_block_count() const{
	STEADY_ASSERT(check_invariant());

	size_t count = internals::divide_round_up(_size, BRANCHING_FACTOR);
	return count;
}

template <class T>
const T* vector<T>::get_block(size_t block_index) const{
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(get_block_count() > 0);
	STEADY_ASSERT(block_index < get_block_count());

	const auto leaf = internals::find_leaf_node(*this, block_index * BRANCHING_FACTOR);
	return &leaf.get_leaf_node()->_values[0];
}



template <class T>
vector<T> vector<T>::push_back(const T& value) const{
	STEADY_ASSERT(check_invariant());

	return internals::push_back_1(*this, value);
}


/*
	### Correct but inefficient.
*/
template <class T>
vector<T> vector<T>::pop_back() const{
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(_size > 0);

	const auto temp = to_vec();
	const auto result = vector<T>(&temp[0], _size - 1);
	return result;
}


/*
	### Correct but inefficient.
*/
#if 0
template <class T>
bool vector<T>::operator==(const vector& rhs) const{
	STEADY_ASSERT(check_invariant());

	const auto a = to_vec();
	const auto b = rhs.to_vec();
	return a == b;
}

#else

template <class T>
bool vector<T>::operator==(const vector& rhs) const{
	STEADY_ASSERT(check_invariant());

	if(_size != rhs._size){
		return false;
	}
	if(_size == 0){
		return true;
	}

	if(_root.get_type() != rhs._root.get_type()){
		return false;
	}
	if(_root.get_type() == internals::node_type::inode && _root.get_inode() == rhs._root.get_inode()){
		return true;
	}
	if(_root.get_type() == internals::node_type::leaf_node && _root.get_leaf_node() == rhs._root.get_leaf_node()){
		return true;
	}

	//	### optimize by comparing node by node, hiearchically.
	//	First check node to see if they are the same pointer. If not, only then compare their values.

	{
		size_t block_count = get_block_count();
		for(size_t index = 0 ; index < block_count ; index++){
			const T* valuesA = get_block(index);
			const T* valuesB = rhs.get_block(index);

			size_t r = std::min(static_cast<size_t>(BRANCHING_FACTOR), _size - index * BRANCHING_FACTOR);
			bool equal = std::equal(valuesA, valuesA + r, valuesB);
			if(!equal){
				return false;
			}
		}
	}

	return true;
}

#endif


template <class T>
vector<T> vector<T>::store(size_t index, const T& value) const{
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(index < _size);

	const auto root = replace_value(_root, _shift, index, value);
	return vector<T>(root, _size, _shift);
}


template <class T>
std::size_t vector<T>::size() const{
	STEADY_ASSERT(check_invariant());

	return _size;
}


template <class T>
T vector<T>::operator[](const std::size_t index) const{
	STEADY_ASSERT(check_invariant());
	STEADY_ASSERT(index < _size);

	const auto leaf = internals::find_leaf_node(*this, index);
	const auto slot_index = index & internals::BRANCHING_FACTOR_MASK;

	STEADY_ASSERT(slot_index < leaf.get_leaf_node()->_values.size());
	const T result = leaf.get_leaf_node()->_values[slot_index];
	return result;
}


#if false

//	### Optimization potential here.
template <class T>
std::vector<T> vector<T>::to_vec() const{
	STEADY_ASSERT(check_invariant());

	std::vector<T> result;
	result.reserve(size());

	for(size_t i = 0 ; i < size() ; i++){
		const auto value = operator[](i);
		result.push_back(value);
	}

	return result;
}

#else

template <class T>
std::vector<T> vector<T>::to_vec() const{
	STEADY_ASSERT(check_invariant());

	std::vector<T> result;
	result.reserve(size());

	//	Block-wise copy.
	size_t block_count = get_block_count();
	for(size_t index = 0 ; index < block_count ; index++){
		const T* valuesA = get_block(index);
		const auto size = std::min(static_cast<size_t>(BRANCHING_FACTOR), _size - index * BRANCHING_FACTOR);
		result.insert(result.end(), valuesA, valuesA + size);
	}
	return result;
}

#endif


template <class T>
void vector<T>::trace_internals() const{
	STEADY_ASSERT(check_invariant());

	STEADY_TRACE_SS("Vector (size: " << _size << ") "
		"total inodes: " << internals::inode<T>::_debug_count << ", "
		"total leaf nodes: " << internals::leaf_node<T>::_debug_count);

	trace_node("", _root);
}



//	### Optimization potential here.
template <class T>
vector<T> operator+(const vector<T>& a, const vector<T>& b){
	vector<T> result;

#if 1
	if(b.empty()){
	}
	else{
		const auto v = b.to_vec();
		result = internals::push_back_batch(a, &v[0], v.size());
	}
#else
	for(size_t i = 0 ; i < b.size() ; i++){
		result = result.push_back(b[i]);
	}
#endif

	STEADY_ASSERT(result.size() == a.size() + b.size());
	return result;
}


}	//	steady
	
#endif /* defined(__steady__vector__) */
