#include "../../Index/UnboundedLCR/BFSIndex.cc"
#include "../../Graph/DGraph.cc"

#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <curses.h>
#include <time.h>
#include <chrono>
#include <thread>

using namespace std;
using namespace graphns;
using namespace indexns;

using namespace std::chrono;

void writeQuerySetToFile(QuerySet& qs, int index, string fileName, bool isTrue)
{
    string newFileName = "";
    if( isTrue )
    {
        newFileName = fileName + to_string(index) + ".true";
    }
    else
    {
        newFileName = fileName + to_string(index) + ".false";
    }

    ofstream queryFile (newFileName);
    if( queryFile.is_open() )
    {
        for(auto q : qs)
        {
            VertexID v = q.first.first;
            VertexID w = q.first.second;
            string ls = to_string( q.second );
            queryFile << v << " " << w << " " << ls << "\n";
        }
    }
    queryFile.close();
}

// This method generates random labelset out of ls with at most nK bits
// either by setting bits in ls to 0 or to 1 randomly
LabelSet generateLabelSet(LabelSet& ls, int nK, int L, std::uniform_int_distribution<int>& distribution, std::default_random_engine& generator)
{
    /*std::this_thread::sleep_for(std::chrono::milliseconds(1));
    double t = fmod(getCurrentTimeInMilliSec()*100000,10000000);
    int s = (int) round(t);

    unsigned int seed = (s) % 10000000;
    //cout << "t=" << t << ",seconds=" << s << ",seed=" << seed << endl;
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, L);*/

    int nL = getNumberOfLabelsInLabelSet(ls);

    while(nL != nK)
    {
        int j = distribution(generator);

        if( nL < nK )
        {
            setLabelInLabelSet(ls, j , 1);
        }

        if( nL > nK )
        {
            setLabelInLabelSet(ls, j , 0);
        }

        nL = getNumberOfLabelsInLabelSet(ls);
        //cout << "nL1 > nK j=" << j << ",nL1=" << nL1 << ",ls2=" << labelSetToString(ls2) << endl;
    }

    return ls;
}

/* This method generate twos labelsets with nK labels: the first one should be true
and the second should be false. This can not be guaranteed.*/
pair<LabelSet, LabelSet> generateQuery(VertexID v, VertexID w, int nK, Graph* graph)
{
    //cout << "generateQuery: v=" << v << ",w=" << w << ",nK=" << nK << endl;

    int A = graph->getNumberOfLabels() / 8 + 1;
    LabelSet ls (A);

    queue< pair<VertexID,LabelSet> > q;
    q.push( make_pair(v, ls) );
    vector<VertexID> visited;

    while( q.empty() == false )
    {
        VertexID w1 = q.front().first;
        LabelSet ls1 = q.front().second;
        q.pop();

        //cout << "w1=" << w1 << ",ls1=" << labelSetToString(ls1) << endl;

        if( w1 == w )
        {
            ls = ls1;
            break;
        }

        if( find(visited.begin(), visited.end(), w1) != visited.end() )
        {
            continue;
        }

        visited.push_back( w1 );

        SmallEdgeSet ses;
        graph->getOutNeighbours(w1, ses);

        for(int i = 0; i < ses.size(); i++)
        {
            VertexID w2 = ses[i].first;
            LabelSet ls2 = ses[i].second;
            LabelSet ls3 = joinLabelSets(ls1, ls2);

            q.push( make_pair(w2,ls3) );
        }
    }

    //cout << "generateQuery ls=" << labelSetToString(ls) << endl;
    int L = graph->getNumberOfLabels();
    LabelSet ls2 = ls;
    LabelSet ls3 = invertLabelSet(ls, L);

    return make_pair( ls2, ls3 );
}

/*
This method generates nqs pairs of query sets (one for true and one for flase). Each query sets has nq queries.
noOfLabels specifies the number of labels for each pair of query sets. The query sets are written to qss.

Queries are generated by picking a random vertex s and looping over all other vertices t. For each s to t 10
random label sets are generated. If s can reach t with such a label set, we add the query to true. Otherwise to
false. Each s can add up to quotum queries to the particular pari fo query sets.

Each query must be between a minimal and maximal difficulty. If over a longer time, no queries are generated we
decrease the minimal and maximal difficulty.
*/
void generateAllQueriesC(int nqs, int nq, QuerySets& qss, BFSIndex* ind, Graph* mg, vector< int >& noOfLabels)
{
    int N = mg->getNumberOfVertices();
    int L = mg->getNumberOfLabels();

    // each query should require at least <MIN_DIFFICULTY> or at most <MAX_DIFFICULTY> steps in a
    // labeled BFS search. The queries in the total query set should have varying
    // difficulties within this range.
    int MAX_DIFFICULTY = 0;
    int MIN_DIFFICULTY = 0;
    if( N >= 500000 )
    {
        MAX_DIFFICULTY = 50 + N/50;
        MIN_DIFFICULTY = 50 + min((int) log2(N), N);
    }
    else
    {
        MAX_DIFFICULTY = max((int) N/10 , 4);
        MIN_DIFFICULTY = min((int) log2(N), N);
    }

    if( MAX_DIFFICULTY <= (MIN_DIFFICULTY-2) )
    {
        MAX_DIFFICULTY = MIN_DIFFICULTY + 2;
    }

    cout << "MAX_DIFFICULTY=" << MAX_DIFFICULTY << ",MIN_DIFFICULTY=" << MIN_DIFFICULTY << endl;

    // two random distributions: one for picking a vertex, the second for
    // picking a difficulty
    unsigned int seed = time(NULL)*time(NULL) % 1000;
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<VertexID> vertexDistribution(0, N - 1);

    seed = (time(NULL)*time(NULL)*time(NULL)) % 1000;
    std::default_random_engine generator2(seed);
    std::uniform_int_distribution<int> diffDistribution(MIN_DIFFICULTY, MAX_DIFFICULTY - 1);

    seed = (2*time(NULL)*time(NULL)*time(NULL)) % 1000;
    std::default_random_engine generator3(seed);
    std::uniform_int_distribution<int> labelDistribution(0, L);

    for(int i = 0; i < 2*nqs; i+=2)
    {
        int nK = noOfLabels[i / 2];

        cout << "genQuery: creating querysets " << i << " (true) and " << (i+1) << " (false) with nK=" << nK << endl;
        cout << "genQuery: ---" << endl;
        int roundNo = 1;
        while( qss[i].size() < nq || qss[i+1].size() < nq )
        {
            int minDiff = max(min(MAX_DIFFICULTY, diffDistribution(generator2)) - roundNo/10 , MIN_DIFFICULTY); // a random minimal difficulty
            VertexID s = vertexDistribution(generator); // a random start point
	    VertexID t = vertexDistribution(generator); // another random point
            int quotum = min(nq, max((nq/100) + (roundNo / 100), 1));

            minDiff = max(1, minDiff - (roundNo/100) ); // minDiff can scale down
            // if no queries are generated

            cout << "genQuery with minDiff=" << minDiff << ",s=" << s << ",quotum=" << quotum << endl;
            vector< int > distances;

            // loop over all nodes that have minimal difficulty
            int c = 0;
	    int tried = 0;
            for(; t < N; t++)
            {
                if( s == t )
                {
                    continue;
                }

                tried++;

                if( c < 2 && tried > min((N/500),100) )
                {
                    break;
                }
                //cout << "genQuery: t=" << t << ",distances[t]=" << distances[t] << endl;

                // try 10 random labelsets from s to t
                int k = 0;
                while(k < quotum)
                {
                    k++;

                    LabelSet ls = 0;
                    ls = generateLabelSet(ls, nK, L, labelDistribution, generator3);

                    bool b = ind->query(s,t,ls);
                    int actualDist = max(ind->getVisitedSetSize(), 0);
                    Query q = make_pair( make_pair(s,t), ls);

                    //cout << "genQuery: ,s=" << s << ",t=" << t << ",minDiff=" << minDiff << ",actualDist=" << actualDist << endl;

                    if( actualDist < minDiff )
                    {
                        continue; // the query should take at least minDiff steps
                    }

                    if( b == true)
                    {
                        if( qss[i].size() < nq && qss[i].count( q ) == 0 )
                        {
                            qss[i].insert( q );
			    cout << "q=(" << s << "," << t << "," << ls << ") with " << b << ",actualDiff=" << actualDist << endl;                            
		            c++;
                        }
                    }
                    else
                    {
                        if( qss[i+1].size() < nq && qss[i+1].count( q ) == 0 )
                        {
                            qss[i+1].insert( q );
                            cout << "q=(" << s << "," << t << "," << ls << ") with " << b << ",actualDiff=" << actualDist << endl;
                            c++;
                        }
                    }

                }

                if( c % quotum == 0 && c > 0 )
                {
                    break;
                }

                if( qss[i].size() == nq && qss[i+1].size() == nq )
                {
                    break;
                }
            }

            //cout << "genQuery: roundNo=" << roundNo << ",qss[i].size()=" << qss[i].size() << ",qss[i+1].size()=" << qss[i+1].size() << endl;
            roundNo++;
        }
    }
    cout << "genQuery: ---" << endl;
}

/*
Kicks off the whole process.
*/
int runGenQuery(string s, int nq, int nqs, vector<int>& noOfLabels)
{
    cout << "s=" << s << ",nqs=" << nqs << ",nq=" << nq << endl;

    DGraph* mg = new DGraph(s);
    BFSIndex* ind = new BFSIndex(mg);

    QuerySets qss (2*nqs);
    for(int i = 0; i < (2*nqs); i++)
    {
        QuerySet qs;
        qss.push_back( qs );
    }

    generateAllQueriesC(nqs, nq, qss, ind, mg, noOfLabels);

    for(int i = 0; i < 2*nqs; i+=2)
    {
        writeQuerySetToFile(qss[i], i/2, s, true);
        writeQuerySetToFile(qss[i+1], i/2, s, false);
    }
}

int main(int argc, char *argv[])
{
    /*
    * Generates a set of queries for a specific file
    * Suppose datagen/graph is the file and we wish to generate k querysets with l queries per queryset.
    *
    * We generate 2k querysets labeled graph0true.txt graph0false.txt ... graph[k-1]true.txt graph[k-1]false.txt
    * In a true file only queries that are equal to true can be found
    * In a false file only queries that are equal to false can be found
    * Then for each of the k querysets we specify k numbers containing the number of labels
    * a query may have, e.g. query (1,2, {a,b}) has two labels whereas  query (1,2, {a,b,c}) has three.
    */

    if( argc < 3 )
    {
        cout << "genQuery <edge file> <k: number of query sets> <l: number of queries per set per true or false> <k numbers denoting number of labels per query>"  << endl;
        return 1;
    }

    string s = argv[1];
    int nqs = atoi(argv[2]);
    int nq = atoi(argv[3]);
    vector< int > noOfLabels;

    if( argc < (3+nqs) )
    {
        cout << "genQuery too few label numbers" << endl;
        cout << "edge file> <k: number of query sets> <l: number of queries per set per true or false> <k numbers denoting number of labels per query>" << endl;
        return 1;
    }

    for(int i = 0; i < nqs; i++)
    {
        int nO = atoi(argv[4+i]);
        cout << "number of labels " << i << ": " << nO << endl;
        noOfLabels.push_back( nO );
    }

    runGenQuery(s, nq, nqs, noOfLabels);

    return 0;
}
