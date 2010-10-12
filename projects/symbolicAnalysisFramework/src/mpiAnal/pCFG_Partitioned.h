#ifndef PARTITIONED_ANALYSIS_H
#define PARTITIONED_ANALYSIS_H

#include "rose.h"
#include <list>
#include <sstream>
#include <iostream>
#include <fstream>
#include <string.h>
using namespace std;

#include "common.h"
#include "variables.h"
#include "cfgUtils.h"
#include "analysisCommon.h"
#include "functionState.h"
#include "latticeFull.h"
#include "analysis.h"
#include "dataflow.h"
#include "VirtualCFGIterator.h"
#include "logical.h"
#include "printAnalysisStates.h"


class IntraPartitionDataflow;
class pCFG_Checkpoint;

// Set of partition dataflow analyses that were split in a given call to split.
// The original analysis that was split is explicitly identified as the "master" of the split.
class partSplit
{
	public:
	set<IntraPartitionDataflow*> splitSet;
	IntraPartitionDataflow* master;
	
	partSplit(IntraPartitionDataflow* master)
	{
		this->master = master;
		splitSet.insert(master);
	}
	
	void addChild(IntraPartitionDataflow* child)
	{
		splitSet.insert(child);
	}
	
	string str(string indent="")
	{
		ostringstream oss;
		
		oss << indent << "[partSplit:\n";
		oss << indent << "    splitSet = <";
		for(set<IntraPartitionDataflow*>::iterator it=splitSet.begin(); it!=splitSet.end(); )
		{
			oss << (*it);
			it++;
			if(it!=splitSet.end()) oss << ", ";
		}
		oss << ">\n";
		oss << indent << "    master = "<< master << "]\n";

		return oss.str();
	}
};

class pCFG_PartitionedAnalysis : virtual public IntraProceduralAnalysis
{
	// Object that we'll be using to perform the analysis on all the partitions
	IntraPartitionDataflow* partitionWorker;
	
	pCFG_PartitionedAnalysis(IntraPartitionDataflow* partitionWorker)
	{
		this->partitionWorker = partitionWorker;
	}
	
	// the partitions that are currently executing
	set<IntraPartitionDataflow*> activeParts;
	// the set of partitions that are currently blocked and need to be explicitly
	// unblocked before they may resume execution
	//set<IntraPartitionDataflow*> blockedParts;
	// the set of partitions that have called join and are simply waiting to be joined
	// to the master partitions that they split from
	set<IntraPartitionDataflow*> joinParts;
	
	// Maps partitions to their respective splits. A given partition may be split
	// multiple times in a hierarchical fashion (split-join). The first split
	// in the list corresponds to the outer-most split and the last split
	// is the inner-most split. Thus, if a given partition performs a join,
	// the jointed split is the last/inner-most split in parts2splits.
	map<IntraPartitionDataflow*, list<partSplit*> > parts2splits;
	
	// Maps analysis partitions to their respective current execution states
	// (these are only upto-date for analyses that are currently stopped)
	map<IntraPartitionDataflow*, pCFG_Checkpoint*> parts2chkpts;
	
	// Sample interprocedural analysis object that we'll use as a factory to create more such objects
	IntraPartitionDataflow* intraFactory;
	
	public:
	// Creates a pCFG_PartitionedAnalysis, starting the analysis with the given
	// master analysis partition and creating an initial split that only contains the master.
	pCFG_PartitionedAnalysis(IntraPartitionDataflow* intraFactory);
	
	// Create the initial partition state for analyzing some function
	void initMaster();
	
	// Returns a reference to the master dataflow analysis. At the end of the partitioned analysis,
	// it is this object that owns all the dataflow state generated by the overall analysis.
	IntraPartitionDataflow* getMasterDFAnalysis();
	
	// Activates the given partition, returning true if it was previously inactive and false otherwise.
	bool activatePart(IntraPartitionDataflow* part);
	
	// Splits the given dataflow analysis partition into several partitions, one for each given checkpoint
	// The partition origA will be assigned the last checkpoint in partitionChkpts.
	// Returns the set of newly-created partitions.
	set<IntraPartitionDataflow*> split(pCFG_FWDataflow* origA, vector<pCFG_Checkpoint*> partitionChkpts,
	                                   const Function& func, NodeState* fState);
	
	// Joins all the analysis partitions in a given split into a single partition, unioning
	// their respective dataflow information
	void join(IntraPartitionDataflow* joinA, pCFG_Checkpoint* chkpt,
	          const Function& func, NodeState* fState);
	
	// Called by the base pCFG_PartitionedAnalysis class when all partitions in a given split have decided 
	//    to join to decide whether they should be joined or all released. It may also do some 
	//    processing of their state prior to the release or join.
	// Returns the set of partitions that will remain in joined status after this join. If all partitions in the split
	//    set are on this list, they are all joined(all but one will be deleted). Any remaining partitions will be released.
	virtual set<IntraPartitionDataflow*> preJoin(partSplit* s, const Function& func, NodeState* fState,
	                                             const map<IntraPartitionDataflow*, 
	                                             pCFG_Checkpoint*>& parts2chkpts)=0;
	
	// Called by the base pCFG_PartitionedAnalysis class when all partitions in a given split have 
	//    finished their respective executions.
	virtual void postFinish(partSplit* s, 
	                        const map<IntraPartitionDataflow*, pCFG_Checkpoint*>& parts2chkpts)=0;
	
	// runs the intra-procedural analysis on the given function, returns true if 
	// the function's NodeState gets modified as a result and false otherwise
	// state - the function's NodeState
	bool runAnalysis(const Function& func, NodeState* state);
};

// Given a source analysis, splits the dataflow states of all the CFG nodes in
// a given function so that the target analysis gets copies of the source analysis'
// state.
class partitionDFAnalysisState: virtual public UnstructuredPassIntraAnalysis
{
	Analysis* srcAnalysis;
	Analysis* tgtAnalysis;
	
	public:
	partitionDFAnalysisState(Analysis* srcAnalysis, Analysis* tgtAnalysis)
	{
		this->srcAnalysis = srcAnalysis;
		this->tgtAnalysis = tgtAnalysis;
	}
	
	void visit(const Function& func, const DataflowNode& n, NodeState& state)
	{
		state.cloneAnalysisState(srcAnalysis, tgtAnalysis);
	}
};

// Given a set of analyses, one of which is designated as a master, unions together the 
// dataflow states associated on each CFG node with each of these analyses.
// The results are associated on each CFG node with the master analysis.
class unionDFAnalysisStatePartitions: virtual public UnstructuredPassIntraAnalysis
{
	set<Analysis*> unionSet;
	Analysis* master;
	
	public:
	unionDFAnalysisStatePartitions(
		set<Analysis*>& unionSet, Analysis* master)
	{
		for(set<Analysis*>::iterator it = unionSet.begin(); it!=unionSet.end(); it++)
		{ this->unionSet.insert(*it); }
		//this->unionSet = unionSet;
		this->master = master;
	}
	
	void visit(const Function& func, const DataflowNode& n, NodeState& state);
};

// Deletes all the state associated with the given analyses
class deleteDFAnalysisState: virtual public UnstructuredPassIntraAnalysis
{
	set<IntraPartitionDataflow*> tgtA;
	
	public:
	deleteDFAnalysisState(set<IntraPartitionDataflow*>& tgtA)
	{
		this->tgtA = tgtA;
	}
	
	void visit(const Function& func, const DataflowNode& n, NodeState& state)
	{
		for(set<IntraPartitionDataflow*>::iterator it = tgtA.begin(); it!=tgtA.end(); it++)
			state.deleteState((Analysis*)*it);
	}
};


class IntraPartitionDataflow : virtual public IntraProceduralDataflow
{
	protected:
	PartitionedAnalysis* parent;
	
	public:
	// The logic expression that describes the invariant that holds for this partition	
	/*LogicalCond*/printable* partitionCond;
	
	IntraPartitionDataflow(const IntraPartitionDataflow& that)
	{
		parent = that.parent;
		partitionCond = that.partitionCond;
	}
	
	IntraPartitionDataflow(PartitionedAnalysis* parent)
	{
		this->parent = parent;
		partitionCond = NULL;
	}
	
	~IntraPartitionDataflow()
	{
		delete partitionCond;
	}
	
/*	IntraPartitionDataflow()
	{
		parent = NULL;
	}
	
	void setParent(PartitionedAnalysis* parent)
	{
		this->parent = parent;	
	}*/
	
	PartitionedAnalysis* getParent() const
	{
		return parent;	
	}
	
	// A dummy analysis that is associated with facts connected with the 
	// IntraPartitionDataflow-specific logic rather than the logic of a class that 
	// derives from IntraPartitionDataflow.
	// Analysis* dummy;
	
	// Creates a new instance of the derived object that is a copy of the original instance.
	// This instance will be used to instantiate a new partition of the analysis.
	virtual IntraPartitionDataflow* copy()=0;
	
	virtual bool runAnalysis(const Function& func, NodeState* fState, bool& splitPart, 
		                      bool &joinPart, pCFG_Checkpoint*& outChkpt)=0;
	
	// Resumes the execution of runAnalysis from the given checkpoint
	// splitPart - set by the call to indicate that it exited because it was split
	// joinPart - set by the call to indicate that it exited because it wishes to join the partitions that it split from
	// outChkpt - set by the call to be the checkpoint that representing the state of this analysis at the time of the exit
	//            set to NULL in the case of a normal exit or a split exit
	virtual bool runAnalysisResume(const Function& func, NodeState* fState, 
	                       pCFG_Checkpoint* chkpt, bool& splitPart, 
	                       bool &joinPart, pCFG_Checkpoint*& outChkpt)=0;
	
	// Called when a partition is created to allow a specific analysis to initialize
	// its dataflow information from the partition condition
	virtual void initDFfromPartCond(const Function& func, const DataflowNode& n, NodeState& state, 
	                                const vector<Lattice*>& dfInfo, const vector<NodeFact*>& facts,
	                                /*LogicalCond*/printable* partitionCond) {}
	
	// Generates the initial lattice state for the given dataflow node, in the given function, with the given NodeState
	virtual void genInitState(const Function& func, const pCFGNode& n, const NodeState& state,
	                          vector<Lattice*>& initLattices, vector<NodeFact*>& initFacts) {}
};


/*class pCFG_SplitConditions : public printable
{
	public:
	// possible split types:
	// dataflowSplit: the process set may take multiple paths through the CFG but all these possible paths 
	//                would be executed by the same process set
	// pSetSplit : the process set itself is split into multiple sub-sets, each of which follows its own 
	//             path through the CFG
	//typedef enum splitType{dataflowSplit, pSetSplit, noSplit};
	
	//splitType type;
	
	// The actual conditions that apply to each path. condition[0] applies to the first outgoing edge, 
	// condition[1] to the second, etc.
	vector<ConstrGraph*> conditions;
	
	~pCFG_SplitConditions();
	
	string str(string indent="");
};*/


#endif
