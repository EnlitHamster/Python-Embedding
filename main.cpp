#include <Python.h>

#include <vtkPythonUtil.h>
#include <vtkObjectBase.h>
#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>

//#define VTK_TEST
//#define VTK_COMPLEX_TEST
//#define VTK_BENCHMARK_NATIVE
#define VTK_BENCHMARK_INTROSPECTION

#if (defined(VTK_TEST) || defined(VTK_COMPLEX_TEST))
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#endif

#ifdef VTK_COMPLEX_TEST
#include <vtkProperty.h>
#endif

#include <unordered_map>
#include <sstream>

#define NOMINMAX
#include <windows.h>


#if (defined(VTK_BENCHMARK_NATIVE) || defined(VTK_BENCHMARK_INTROSPECTION))
#include <chrono>
#include <utility>
#include <fstream>

typedef std::chrono::high_resolution_clock::time_point time_var;

#define DURATION(a) std::chrono::duration_cast<std::chrono::nanoseconds>(a).count()
#define TIME_NOW() std::chrono::high_resolution_clock::now()

std::unordered_map<LPCSTR, double> time_execution_data;

template<typename R, typename F, typename... A>
R timed_execution(LPCSTR name, F function, A&& ...argv)
{
	time_var start = TIME_NOW();
	R r = function(std::forward<A>(argv)...);
	time_execution_data.insert(std::make_pair(name, DURATION(TIME_NOW() - start) / 1000000000.0f));
	return r;
}

template<typename F, typename... A>
void timed_execution_v(LPCSTR name, F function, A&& ...argv)
{
	time_var start = TIME_NOW();
	function(std::forward<A>(argv)...);
	time_execution_data.insert(std::make_pair(name, DURATION(TIME_NOW() - start) / 1000000000.0f));
}
#endif /* VTK_BENCHMARK */


/*
 * Mapping from VTK object to its node in the ClassTree.
 */
static std::unordered_map<vtkObjectBase *, PyObject *> nodes;


/*
 * Initializes Python interpreter and the Introspection object.
 */
PyObject *PyVtk_InitIntrospector()
{
	/* Initializing Python environment and setting PYTHONPATH. */
	Py_Initialize();

	/* Both the "." and cwd notations are left in for security, as after being built in
	   a DLL they may change. */
	PyRun_SimpleString("import sys\nimport os");
	PyRun_SimpleString("sys.path.append( os.path.dirname(os.getcwd()) )");
	PyRun_SimpleString("sys.path.append(\".\")");

	/* Decode module from its name. Returns error if the name is not decodable. */
	PyObject *pIntrospectorModuleName = PyUnicode_DecodeFSDefault("Introspector");
	if (pIntrospectorModuleName == NULL)
	{
		fprintf(stderr, "Fatal error: cannot decode module name\n");
		return NULL;
	}

	/* Imports the module previously decoded. Returns error if the module is not found. */
	PyObject *pIntrospectorModule = PyImport_Import(pIntrospectorModuleName);
	Py_DECREF(pIntrospectorModuleName);
	if (pIntrospectorModule == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Failed to load \"Introspector\"\n");
		return NULL;
	}

	/* Looks for the Introspector class in the module. If it does not find it, returns and error. */
	PyObject* pIntrospectorClass = PyObject_GetAttrString(pIntrospectorModule, "Introspector");
	Py_DECREF(pIntrospectorModule);
	if (pIntrospectorClass == NULL || !PyCallable_Check(pIntrospectorClass))
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot find class \"Introspector\"\n");
		if (pIntrospectorClass != NULL)
		{
			Py_DECREF(pIntrospectorClass);
		}
		return NULL;
	}

	/* Instantiates an Introspector object. If the call returns NULL there was an error
	   creating the object, and thus it returns error. */
	PyObject *pIntrospector = PyObject_CallObject(pIntrospectorClass, NULL);
	Py_DECREF(pIntrospectorClass);
	if (pIntrospector == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Introspector instantiation failed\n");
		return NULL;
	}

	return pIntrospector;
}


vtkObjectBase *PyVtk_CreateVtkObject(
	PyObject *pIntrospector,
	const char *sVtkClassName)
{
	/* Creating the object and getting the reference. Returns error if the object could not
       be created.*/
	PyObject *pPyVtkObject = PyObject_CallMethod(pIntrospector, "createVtkObject", "s", sVtkClassName);
	if (pPyVtkObject == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot call \"createVtkObject\" on \"%s\"\n", sVtkClassName);
		return NULL;
	}

	/* Retrieving vtk instance. Returns error if it cannot access the vtk instance. */
	PyObject *pPyVtkInstance = PyObject_GetAttrString(pPyVtkObject, "vtkInstance");
	if (pPyVtkInstance == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot access \"vtkInstance\" of VTK wrapped object\n");
		return NULL;
	}

	/* Retrieving C object from vtk instance */
	vtkObjectBase *pVtkObject = vtkPythonUtil::GetPointerFromObject(pPyVtkInstance, sVtkClassName);
	Py_DECREF(pPyVtkInstance);

	/* Adding a node entry to the vtk objects - nodes map. */
	nodes.insert(std::make_pair(pVtkObject, pPyVtkObject));

	return pVtkObject;
}


const char *PyVtk_GetVtkObjectProperty(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	LPCSTR propertyName,
	LPCSTR expectedType)
{
	/* Retriving node from registry. Returns error if the VTK object has no node. */
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;
		
		/* Retrieving the property value. Returns error if there is no property with the given name. */
		PyObject *pVal = PyObject_CallMethod(pIntrospector, "getVtkObjectAttribute", "Os", pNode, propertyName);
		if (pVal == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot access the VTK object's attribute \"%s\"\n", propertyName);
			return NULL;
		}

		/* Converting the value to string. Returns error if unable to. */
		const char* propertyValue = PyString_AsString(pVal);
		Py_DECREF(pVal);
		if (propertyValue == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot convert attribute \"%s\" to string\n", propertyName);
			return NULL;
		}

		/* returning decorated version of the value. */
		std::stringstream buffer;
		buffer << expectedType << "::" << propertyValue;
		return strdup(buffer.str().c_str());
	}
	else
	{
		return NULL;
	}
}


void PyVtk_SetVtkObjectProperty(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	LPCSTR propertyName,
	LPCSTR format,
	LPCSTR newValue)
{
	/* Retriving node from registry. Returns error if the VTK object has no node. */
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Executing method call to set value. Returns error if the value could not be set. */
		PyObject *pCheck = PyObject_CallMethod(pIntrospector, "setVtkObjectAttribute", "Osss", pNode, propertyName, format, newValue);
		if (pCheck == NULL)
		{
			fprintf(stderr, "Cannot set the VTK object's attribute \"%s\"\n", propertyName);
			return;
		}
		Py_DECREF(pCheck);
	}
}


const char *PyVtk_GetVtkObjectDescriptor(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject)
{
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Retrieving the descriptor. Returns error if the descriptor could not be built. */
		PyObject *pDescriptor = PyObject_CallMethod(pIntrospector, "getVtkObjectDescriptor", "O", pNode);
		if (pDescriptor == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot access the VTK object's descriptor\n");
			return NULL;
		}

		/* Converting the value to string. Returns error if unable to. */
		const char* descriptor = PyString_AsString(pDescriptor);
		Py_DECREF(pDescriptor);
		if (descriptor == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot convert descriptor to string\n");
			return NULL;
		}

		return descriptor;
	}
	else
	{
		return NULL;
	}
}


bool PyVtk_DeleteVtkObject(
	PyObject *pIntrospector,
	vtkObjectBase* pVtkObject)
{
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Executing method call to set value. Returns error if the value could not be set. */
		PyObject *pCheck = PyObject_CallMethod(pIntrospector, "deleteVtkObject", "O", pNode);
		if (pCheck == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot delete the VTK object\n");
			return false;
		}
		Py_DECREF(pCheck);

		/* Freeing the node's space and cleaning up. */
		Py_DECREF(pNode);
		nodes.erase(pVtkObject);

		return true;
	}
	else
	{
		return false;
	}
}


vtkAlgorithmOutput *PyVtk_GetOutputPort(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject)
{
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Executing method call to get the port. Returns error if the port could not be accessed. */
		PyObject *pPyPort = PyObject_CallMethod(pIntrospector, "getVtkObjectOutputPort", "O", pNode);
		if (pPyPort == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot access the VTK object output port\n");
			return NULL;
		}

		/* Extracting the output port and connecting. */
		return (vtkAlgorithmOutput *)vtkPythonUtil::GetPointerFromObject(pPyPort, "vtkAlgorithmOutput");
	}
	else
	{
		return NULL;
	}
}


bool PyVtk_ConnectVtkObject(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	vtkAlgorithm *pVtkTarget)
{
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Executing method call to get the port. Returns error if the port could not be accessed. */
		PyObject *pPyPort = PyObject_CallMethod(pIntrospector, "getVtkObjectOutputPort", "O", pNode);
		if (pPyPort == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Cannot access the VTK object output port\n");
			return false;
		}

		/* Extracting the output port and connecting. */
		vtkAlgorithmOutput *pPort = (vtkAlgorithmOutput *) vtkPythonUtil::GetPointerFromObject(pPyPort, "vtkAlgorithmOutput");
		pVtkTarget->SetInputConnection(pPort);

		return true;
	}
	else
	{
		return false;
	}
}


void PyVtk_FinalizeIntrospector(
	PyObject *pIntrospector)
{
	for (auto iNode : nodes)
	{
		vtkObjectBase *pVtkObject = iNode.first;
		PyVtk_DeleteVtkObject(pIntrospector, pVtkObject);
	}

	Py_DECREF(pIntrospector);
	Py_Finalize();

	if (PyErr_Occurred())
	{
		PyErr_Print();
	}
}


size_t argsize(LPCSTR str)
{
	size_t size = 0;
	size_t maxsize = std::strlen(str);
	for (int i = 0; i < maxsize; ++i)
	{
		if (isalpha(str[i]))
		{
			++size;
		}
	}

	return size;
}


PyObject *PyVtk_ArgvTuple(
	LPCSTR format,
	size_t argc,
	std::vector<vtkObjectBase *> pReferences,
	std::vector<LPCSTR> argv)
{
	PyObject *pArgs = PyTuple_New(argc);
	if (pArgs == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Unable to create a tuple of size %d\n", argc);
		return NULL;
	}

	/* Populating arguments. */
	for (int i = 0; i < argc; ++i)
	{
		int objects = 0;
		int values = 0;
		PyObject *pVal = NULL;

		char def = format[i];

		/* Checking for size specifications */
		std::stringstream strspec;
		int di = 1;
		while (isdigit(format[i + di]))
		{
			strspec << format[i + di];
			++di;
		}

		int spec = 0;
		if (di > 1)
		{
			spec = std::stoi(strspec.str());
			i += di - 1;
		}

		if (def == 'o' || def == 'O')
		{
			/* Getting the object's reference. */
			auto iNodeRef = nodes.find(pReferences[objects]);
			if (nodes.end() != iNodeRef)
			{
				pVal = iNodeRef->second;
				++objects;
			}
			else
			{
				/* The object may be wrappable as a VTK object. */
				pVal = vtkPythonUtil::GetObjectFromPointer(pReferences[objects]);

				if (pVal == NULL)
				{
					if (PyErr_Occurred())
					{
						PyErr_Print();
					}
					fprintf(stderr, "Reference out of bound %d\n", objects);
					Py_XDECREF(pArgs);
					return NULL;
				}
			}
		}
		else
		{
			/* Getting the string. */
			if (argv[values] == NULL)
			{
				if (PyErr_Occurred())
				{
					PyErr_Print();
				}
				fprintf(stderr, "Value out of bound %d\n", values);
				Py_XDECREF(pArgs);
				return NULL;
			}

			/* Unspec-ed argument */
			if (spec == 0)
			{
				const char *str = argv[values++];

				switch (def)
				{
				case 's':
				case 'S':
					pVal = PyString_FromString(str);
					break;

				case 'd':
				case 'D':
					pVal = PyLong_FromLong(std::stol(str, nullptr));
					break;

				case 'f':
				case 'F':
					pVal = PyFloat_FromDouble(std::stod(str, nullptr));
					break;

				case 'b':
				case 'B':
					pVal = PyBool_FromLong(std::stol(str, nullptr));
					break;

				default:
					if (PyErr_Occurred())
					{
						PyErr_Print();
					}
					fprintf(stderr, "Format no recognised %c\n", def);
					Py_XDECREF(pArgs);
					return NULL;
				}
			}
			/* Spec-ed argument */
			else
			{
				PyObject *pTuple = PyTuple_New(spec);
				if (pTuple == NULL)
				{
					if (PyErr_Occurred())
					{
						PyErr_Print();
					}
					fprintf(stderr, "Unable to create a tuple of size %d\n", spec);
					Py_XDECREF(pArgs);
					return NULL;
				}

				for (int j = 0; j < spec; ++j)
				{
					const char *str = argv[values++];

					switch (def)
					{
					case 's':
					case 'S':
						PyTuple_SET_ITEM(pTuple, j, PyString_FromString(str));
						break;

					case 'd':
					case 'D':
						PyTuple_SET_ITEM(pTuple, j, PyLong_FromLong(std::stol(str, nullptr)));
						break;

					case 'f':
					case 'F':
						PyTuple_SET_ITEM(pTuple, j, PyFloat_FromDouble(std::stod(str, nullptr)));
						break;

					case 'b':
					case 'B':
						PyTuple_SET_ITEM(pTuple, j, PyBool_FromLong(std::stol(str, nullptr)));
						break;

					default:
						if (PyErr_Occurred())
						{
							PyErr_Print();
						}
						fprintf(stderr, "Format no recognised %c", def);
						Py_XDECREF(pTuple);
						Py_XDECREF(pArgs);
						return NULL;
					}
				}

				pVal = pTuple;
			}
		}

		if (pVal == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Argument number %d is not encodable with type \"%c\"\n", i, def);
			return NULL;
		}
		PyTuple_SET_ITEM(pArgs, i, pVal);
	}

	return pArgs;
}


PyObject *PyVtk_ObjectMethod(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	LPCSTR method,
	LPCSTR format,
	std::vector<vtkObjectBase *> pReferences,
	std::vector<LPCSTR> argv)
{
	/* Retrieving node from registry. Returns error if the VTK object has no node. */
	auto iNode = nodes.find(pVtkObject);
	if (nodes.end() != iNode)
	{
		/* Getting Python node. */
		PyObject *pNode = iNode->second;

		/* Generating argument list. */
		size_t argc = argsize(format);
		PyObject *pArgs = PyVtk_ArgvTuple(format, argc, pReferences, argv);
		if (pArgs == NULL)
		{
			/* Escalating error. */
			return NULL;
		}

		/* Calling the method. */
		PyObject *pReturn = PyObject_CallMethod(pIntrospector, "vtkInstanceCall", "OsO", pNode, method, pArgs);
		Py_XDECREF(pArgs);
		if (pReturn == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Method \"%s\" call resulted in error\n", method);
			return NULL;
		}

		return pReturn;
	}
	else
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot find node\n");
		return NULL;
	}
}


vtkObjectBase *PyVtk_ObjectMethodAsVtkObject(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	LPCSTR method,
	LPCSTR vtkClassname,
	LPCSTR format,
	std::vector<vtkObjectBase *> pReferences,
	std::vector<LPCSTR> argv)
{
	/* Calling the method and getting encoded result. */
	PyObject *pVal = PyVtk_ObjectMethod(pIntrospector, pVtkObject, method, format, pReferences, argv);
	if (pVal == NULL)
	{
		/* Escalating the error. */
		return NULL;
	}

	/* Retrieving VTK Object. */
	for (auto node : nodes)
	{
		if (node.second == pVal)
		{
			return node.first;
		}
	}

	/* The VTK object is not yet registered. Registering it now. */
	PyObject *pNewNode = PyObject_CallMethod(pIntrospector, "createVtkObjectWithInstance", "sO", vtkClassname, pVal);
	if (pNewNode == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot create node for new object.\n");
		return NULL;
	}

	/* Retrieving C object from vtk instance */
	vtkObjectBase *pReturnVtkObject = vtkPythonUtil::GetPointerFromObject(pVal, vtkClassname);
	Py_DECREF(pVal);

	/* Adding a node entry to the vtk objects - nodes map. */
	nodes.insert(std::make_pair(pReturnVtkObject, pNewNode));

	return pReturnVtkObject;
}


static void argsize(LPCSTR str, size_t *pRefs, size_t *pVals)
{
	size_t maxsize = std::strlen(str);
	for (int i = 0; i < maxsize; ++i)
	{
		if (str[i] == 'o' || str[i] == 'O')
		{
			++(*pRefs);
		}
		else if (isalpha(str[i]))
		{
			++(*pVals);
		}
	}
}


/*
 * Intermediaries need to be VTK objects
 */
PyObject *PyVtk_PipedObjectMethod(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	std::vector<LPCSTR> methods,
	std::vector<LPCSTR> formats,
	std::vector<vtkObjectBase *> pReferences,
	std::vector<LPCSTR> argv)
{
#ifdef PYTHON_EMBED_LOG
	VtkIntrospection::log << "called VtkIntrospection::PipedObjectMethod with pVtkObject = " << pVtkObject << ", method = " << method << ", format = " << format << std::endl;
	VtkIntrospection::log.flush();
#endif

	/* Getting the first arguments. */
	LPCSTR method = methods[0];
	LPCSTR format = formats[0];
	size_t refc = 0;
	size_t valc = 0;
	argsize(format, &refc, &valc);
	std::vector<vtkObjectBase *> pRefs(pReferences.begin(), pReferences.begin() + refc);
	std::vector<LPCSTR> pArgv(argv.begin(), argv.begin() + valc);

	/* First call is on a node. Further calls are not. */
	PyObject *pPipedCaller = PyVtk_ObjectMethod(
		pIntrospector, pVtkObject, method, format, pRefs, pArgv);

	for (int i = 1; i < methods.size(); ++i)
	{
		/* Getting new arguments. */
		method = methods[i];
		format = formats[i];
		size_t oldrefc = refc;
		size_t oldvalc = valc;
		argsize(format, &refc, &valc);
		pRefs.assign(pReferences.begin() + oldrefc, pReferences.begin() + refc);
		pArgv.assign(argv.begin() + oldvalc, argv.begin() + valc);

		/* Call on next piped element. */
		PyObject *pArgs = PyVtk_ArgvTuple(format, (refc - oldrefc) + (valc - oldvalc), pRefs, pArgv);
		if (pArgs == NULL)
		{
			/* Escalating error. */
			return NULL;
		}

		/* Getting the next pipe object. */
		PyObject *pNextPipedCaller = PyObject_CallMethod(pIntrospector, "genericCall", "OsO", pPipedCaller, method, pArgs);
		if (pNextPipedCaller == NULL)
		{
			if (PyErr_Occurred())
			{
				PyErr_Print();
			}
			fprintf(stderr, "Could not call \"%s\".", method);
			return NULL;
		}

		/* Swapping to next caller. */
		Py_DECREF(pPipedCaller);
		pPipedCaller = pNextPipedCaller;
	}

	/* Returning the last return value. */
	return pPipedCaller;
}


LPCSTR PyVtk_PipedObjectMethodAsString(
	PyObject *pIntrospector,
	vtkObjectBase *pVtkObject,
	std::vector<LPCSTR> methods,
	std::vector<LPCSTR> formats,
	std::vector<vtkObjectBase *> pReferences,
	std::vector<LPCSTR> argv)
{
	/* Calling the method and getting encoded result. */
	PyObject *pVal = PyVtk_PipedObjectMethod(pIntrospector, pVtkObject, methods, formats, pReferences, argv);
	if (pVal == NULL)
	{
		/* Escalating the error. */
		return NULL;
	}

	/* Decoding return value. */
	PyObject *pReturn = PyObject_CallMethod(pIntrospector, "outputFormat", "(O)", pVal);
	Py_DECREF(pVal);
	if (pReturn == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Unable to decode return value.\n");
		return NULL;
	}

	/* Extracting string value. */
	LPCSTR str = strdup(PyString_AsString(pReturn));
	Py_DECREF(pReturn);

	return str;
}


std::vector<LPCSTR> split(
	LPCSTR str, 
	char split)
{
	std::vector<LPCSTR> sstr;
	std::istringstream stream(str);
	std::string lstr;
	while (getline(stream, lstr, split))
	{
		sstr.emplace_back(lstr.c_str());
	}
	return sstr;
}


#ifdef VTK_BENCHMARK_INTROSPECTION
void test_introspection()
{

	PyObject *pIntrospector = timed_execution<PyObject *>("interpreter_init", PyVtk_InitIntrospector);

	vtkObjectBase
		*pReader = timed_execution<vtkObjectBase *>("reader_inst", PyVtk_CreateVtkObject, pIntrospector, "vtkStructuredGridReader"),
		*pSeeds = timed_execution<vtkObjectBase *>("seeds_inst", PyVtk_CreateVtkObject, pIntrospector, "vtkPointSource"),
		*pStreamer = timed_execution<vtkObjectBase *>("streamer_inst", PyVtk_CreateVtkObject, pIntrospector, "vtkStreamTracer"),
		*pOutline = timed_execution<vtkObjectBase *>("outline_inst", PyVtk_CreateVtkObject, pIntrospector, "vtkStructuredGridOutlineFilter");

	timed_execution_v("reader_setfile", PyVtk_SetVtkObjectProperty, pIntrospector, pReader, "FileName", "s", "density.vtk");

	timed_execution_v("reader_update", PyVtk_ObjectMethod, // pReader->Update()
		pIntrospector,
		pReader,
		"Update",
		"",
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());


	PyObject *pOutput = timed_execution<PyObject *>("reader_getoutput", PyVtk_ObjectMethod, // pReader->GetOutput()
		pIntrospector,
		pReader,
		"GetOutput",
		"",
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());

	LPCSTR center = timed_execution<LPCSTR>("reader_getoutput_getcenter", PyVtk_PipedObjectMethodAsString, // pReader->GetOutput()->GetCenter()
		pIntrospector,
		pReader,
		std::vector<LPCSTR>({ "GetOutput", "GetCenter" }),
		std::vector<LPCSTR>({ "", "" }),
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());

	timed_execution_v("seeds_setradius", PyVtk_SetVtkObjectProperty, pIntrospector, pSeeds, "Radius", "f", "3.0");
	timed_execution_v("seeds_setcenter", PyVtk_SetVtkObjectProperty, pIntrospector, pSeeds, "Center", "f3", center);
	timed_execution_v("seeds_setnumberofpoints", PyVtk_SetVtkObjectProperty, pIntrospector, pSeeds, "NumberOfPoints", "d", "100");

	vtkAlgorithmOutput *pSeedsPort = timed_execution<vtkAlgorithmOutput *>("seeds_getoutputport", PyVtk_GetOutputPort, pIntrospector, pSeeds);

	timed_execution_v("streamer_setinputconn_reader", PyVtk_ConnectVtkObject, pIntrospector, pReader, (vtkAlgorithm *)pStreamer);
	timed_execution_v("streamer_setsourceconn_seeds", PyVtk_ObjectMethod, // pStreamer->SetSourceConnection(pSeeds->GetOutputPort(0))
		pIntrospector,
		pStreamer,
		"SetSourceConnection",
		"o",
		std::vector<vtkObjectBase *>({ pSeedsPort }),
		std::vector<LPCSTR>());

	timed_execution_v("streamer_setmaxpropagation", PyVtk_SetVtkObjectProperty, pIntrospector, pStreamer, "MaximumPropagation", "d", "100");
	timed_execution_v("streamer_setinitialintegstep", PyVtk_SetVtkObjectProperty, pIntrospector, pStreamer, "InitialIntegrationStep", "f", "0.1");
	timed_execution_v("streamer_setintegdirboth", PyVtk_ObjectMethod, // pStreamer->SetIntegrationDirectionToBoth()
		pIntrospector,
		pStreamer,
		"SetIntegrationDirectionToBoth",
		"",
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());

	timed_execution_v("outline_setinputconn", PyVtk_ConnectVtkObject, pIntrospector, pReader, (vtkAlgorithm *)pOutline);

	timed_execution_v("interpreter_fin", PyVtk_FinalizeIntrospector, pIntrospector);
}
#endif /* VTK_BENCHMARK_INTROSPECTION */


#ifdef VTK_BENCHMARK_NATIVE
PyObject *init_python()
{
	/* Initializing Python environment and setting PYTHONPATH. */
	Py_Initialize();

	/* Both the "." and cwd notations are left in for security, as after being built in
	   a DLL they may change. */
	PyRun_SimpleString("import sys\nimport os");
	PyRun_SimpleString("sys.path.append( os.path.dirname(os.getcwd()) )");
	PyRun_SimpleString("sys.path.append(\".\")");

	/* Decode module from its name. Returns error if the name is not decodable. */
	PyObject *pVtkModuleName = PyUnicode_DecodeFSDefault("vtk");
	if (pVtkModuleName == NULL)
	{
		fprintf(stderr, "Fatal error: cannot decode module name\n");
		return NULL;
	}

	/* Imports the module previously decoded. Returns error if the module is not found. */
	PyObject *pVtkModule = PyImport_Import(pVtkModuleName);
	Py_DECREF(pVtkModuleName);
	if (pVtkModule == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Failed to load \"Introspector\"\n");
		return NULL;
	}

	return pVtkModule;
}


PyObject *inst_vtkobj(
	PyObject *pVtkModule,
	LPCSTR classname)
{
	/* Looks for the Introspector class in the module. If it does not find it, returns and error. */
	PyObject* pVtkClass = PyObject_GetAttrString(pVtkModule, classname);
	if (pVtkClass == NULL || !PyCallable_Check(pVtkClass))
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Cannot find class \"Introspector\"\n");
		if (pVtkClass != NULL)
		{
			Py_DECREF(pVtkClass);
		}
		return NULL;
	}

	/* Instantiates an Introspector object. If the call returns NULL there was an error
	   creating the object, and thus it returns error. */
	PyObject *pInstance = PyObject_CallObject(pVtkClass, NULL);
	Py_DECREF(pVtkClass);
	if (pInstance == NULL)
	{
		if (PyErr_Occurred())
		{
			PyErr_Print();
		}
		fprintf(stderr, "Introspector instantiation failed\n");
		return NULL;
	}

	return pInstance;
}


void test_native()
{
	PyObject *pVtkModule = timed_execution<PyObject *>("python_inst", init_python);

	PyObject *pReader = timed_execution<PyObject *>("reader_inst", inst_vtkobj, pVtkModule, "vtkStructuredGrid");
	timed_execution_v("reader_setfile", PyObject_CallMethod, pReader, "SetFileName", "s", "density.vtk");
	timed_execution_v("reader_update", PyObject_CallMethod, pReader, "Update", "");

	PyObject *pSeeds = timed_execution<PyObject *>("seeds_inst", inst_vtkobj, pVtkModule, "vtkPointSource");
	timed_execution_v("seeds_setradius", PyObject_CallMethod, pSeeds, "SetRadius", "d", 3.0);
	PyObject *pOutput = timed_execution<PyObject *>("reader_getoutput", PyObject_CallMethod, pReader, "GetOutput", "");
	PyObject *pCenter = timed_execution<PyObject *>("output_getcenter", PyObject_CallMethod, pOutput, "GetCenter", "");
	timed_execution_v("seeds_setcenter", PyObject_CallMethod, pSeeds, "SetCenter", "(d,d,d)", pCenter);
	timed_execution_v("seeds_setnumberofpoints", PyObject_CallMethod, pSeeds, "SetNumberOfPoints", "i", 100);

	PyObject *pStreamer = timed_execution<PyObject *>("streamer_inst", inst_vtkobj, pVtkModule, "vtkStreamTracer");
	PyObject *pReaderPort = timed_execution<PyObject *>("reader_getoutputport", PyObject_CallMethod, pReader, "GetOutputPort", "i", 0);
	PyObject *pSeedsPort = timed_execution<PyObject *>("seeds_getoutputport", PyObject_CallMethod, pSeeds, "GetOutputPort", "i", 0);
	timed_execution_v("streamer_setinputconn", PyObject_CallMethod, pStreamer, "SetInputConnection", "O", pReaderPort);
	timed_execution_v("streamer_setsourceconn", PyObject_CallMethod, pStreamer, "SetSourceConnection", "O", pSeedsPort);
	timed_execution_v("streamer_setmaxpropagation", PyObject_CallMethod, pStreamer, "SetMaximumPropagation", "i", 1000);
	timed_execution_v("streamer_setinitialintegstep", PyObject_CallMethod, pStreamer, "SetInitialIntegrationStep", "d", 0.1);
	timed_execution_v("streamer_setintegdirboth", PyObject_CallMethod, pStreamer, "SetIntegrationDirectionToBoth", "");

	PyObject *pOutline = timed_execution<PyObject *>("outline_inst", inst_vtkobj, pVtkModule, "vtkStructuredGridOutlineFilter");
	timed_execution_v("outline_setinputconn", PyObject_CallMethod, pOutline, "SetInputConnection", "O", pReaderPort);

	Py_XDECREF(pVtkModule);
	Py_XDECREF(pReader);
	Py_XDECREF(pSeeds);
	Py_XDECREF(pStreamer);
	Py_XDECREF(pOutline);

	timed_execution_v("python_fin", Py_Finalize);
}
#endif /* VTK_BENCHMARK_NATIVE */


int main(int argc, char *argv[])
{
#ifdef VTK_TEST
	PyObject *pIntrospector = PyVtk_InitIntrospector();
	if (pIntrospector == NULL)
	{
		printf("Initialization failed\n");
	}

	vtkObjectBase *pConeSource = PyVtk_CreateVtkObject(pIntrospector, "vtkConeSource");
	if (pConeSource == NULL)
	{
		printf("Object creation failed\n");
	}

	const char *height = PyVtk_GetVtkObjectProperty(pIntrospector, pConeSource, "Height", "f");
	if (height == NULL)
	{
		printf("Get object attribute failed\n");
	}

	PyVtk_SetVtkObjectProperty(pIntrospector, pConeSource, "Height", "f", "2.0");

	const char *newHeight = PyVtk_GetVtkObjectProperty(pIntrospector, pConeSource, "Height", "f");
	if (strcmp(height, newHeight) == 0)
	{
		printf("Set-Get object attribute failed\n");
	}

	const char *descriptor = PyVtk_GetVtkObjectDescriptor(pIntrospector, pConeSource);
	if (descriptor == NULL)
	{
		printf("Get object descriptor failed\n");
	}

	vtkNew<vtkPolyDataMapper> pMapper;
	if (!PyVtk_ConnectVtkObject(pIntrospector, pConeSource, pMapper))
	{
		printf("Connect object failed\n");
	}

	vtkNew<vtkActor> pActor;
	pActor->SetMapper(pMapper);

	vtkNew<vtkRenderer> pRenderer;
	vtkNew<vtkRenderWindow> pRenderWindow;
	pRenderWindow->AddRenderer(pRenderer);
	pRenderWindow->SetSize(640, 480);

	vtkNew<vtkRenderWindowInteractor> pRenderWindowInteractor;
	pRenderWindowInteractor->SetRenderWindow(pRenderWindow);

	pRenderer->AddActor(pActor);
	
	pRenderWindow->SetWindowName("Cone");
	pRenderWindow->Render();
	pRenderWindowInteractor->Start();

	PyVtk_FinalizeIntrospector(pIntrospector);
#endif /* VTK_TEST */

#ifdef VTK_COMPLEX_TEST
	PyObject *pIntrospector = PyVtk_InitIntrospector();

	vtkObjectBase
		*pReader = PyVtk_CreateVtkObject(pIntrospector, "vtkStructuredGridReader"),
		*pSeeds = PyVtk_CreateVtkObject(pIntrospector, "vtkPointSource"),
		*pStreamer = PyVtk_CreateVtkObject(pIntrospector, "vtkStreamTracer"),
		*pOutline = PyVtk_CreateVtkObject(pIntrospector, "vtkStructuredGridOutlineFilter");

	PyVtk_SetVtkObjectProperty(pIntrospector, pReader, "FileName", "s", "density.vtk");

	PyVtk_ObjectMethod( // pReader->Update()
		pIntrospector, 
		pReader, 
		"Update", 
		"", 
		std::vector<vtkObjectBase *>(), 
		std::vector<LPCSTR>());
	
	LPCSTR center = PyVtk_PipedObjectMethodAsString( // pReader->GetOutput()->GetCenter()
		pIntrospector,
		pReader,
		std::vector<LPCSTR>({ "GetOutput", "GetCenter" }),
		std::vector<LPCSTR>({ "", "" }),
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());

	PyVtk_SetVtkObjectProperty(pIntrospector, pSeeds, "Radius", "f", "3.0");
	PyVtk_SetVtkObjectProperty(pIntrospector, pSeeds, "Center", "f3", center);
	//PyVtk_SetVtkObjectProperty(pIntrospector, pSeeds, "Center", "f3", "0.0,0.0,0.0");
	PyVtk_SetVtkObjectProperty(pIntrospector, pSeeds, "NumberOfPoints", "d", "100");

	vtkAlgorithmOutput *pSeedsPort = PyVtk_GetOutputPort(pIntrospector, pSeeds);

	PyVtk_ConnectVtkObject(pIntrospector, pReader, (vtkAlgorithm *)pStreamer);
	PyVtk_ObjectMethod( // pStreamer->SetSourceConnection(pSeeds->GetOutputPort(0))
		pIntrospector,
		pStreamer,
		"SetSourceConnection",
		"o",
		std::vector<vtkObjectBase *>({ pSeedsPort }),
		std::vector<LPCSTR>());

	PyVtk_SetVtkObjectProperty(pIntrospector, pStreamer, "MaximumPropagation", "d", "100");
	PyVtk_SetVtkObjectProperty(pIntrospector, pStreamer, "InitialIntegrationStep", "f", "0.1");
	PyVtk_ObjectMethod( // pStreamer->SetIntegrationDirectionToBoth()
		pIntrospector,
		pStreamer,
		"SetIntegrationDirectionToBoth",
		"",
		std::vector<vtkObjectBase *>(),
		std::vector<LPCSTR>());

	PyVtk_ConnectVtkObject(pIntrospector, pReader, (vtkAlgorithm *)pOutline);

	vtkNew<vtkPolyDataMapper> 
		pStreamerMapper,
		pOutlineMapper;

	pStreamerMapper->SetInputConnection(PyVtk_GetOutputPort(pIntrospector, pStreamer));
	pOutlineMapper->SetInputConnection(PyVtk_GetOutputPort(pIntrospector, pOutline));

	vtkNew<vtkActor>
		pStreamerActor,
		pOutlineActor;

	pStreamerActor->SetMapper(pStreamerMapper);
	pOutlineActor->SetMapper(pOutlineMapper);

	pOutlineActor->GetProperty()->SetColor(0.0f, 0.0f, 1.0f);
	pOutlineActor->GetProperty()->SetOpacity(1.0f);
	pStreamerActor->GetProperty()->SetColor(0.0f, 0.0f, 1.0f);
	pStreamerActor->GetProperty()->SetOpacity(1.0f);

	vtkNew<vtkRenderer> pRenderer;
	vtkNew<vtkRenderWindow> pRenderWindow;
	pRenderWindow->AddRenderer(pRenderer);
	pRenderWindow->SetSize(640, 480);

	vtkNew<vtkRenderWindowInteractor> pRenderWindowInteractor;
	pRenderWindowInteractor->SetRenderWindow(pRenderWindow);

	pRenderer->AddActor(pStreamerActor);
	pRenderer->AddActor(pOutlineActor);

	pRenderWindow->SetWindowName("Streamer");
	pRenderWindow->Render();
	pRenderWindowInteractor->Start();

	PyVtk_FinalizeIntrospector(pIntrospector);
#endif /* VTK_COMLPEX_TEST */

#ifdef VTK_BENCHMARK_NATIVE
	timed_execution_v("main", test_native);

	// Setup
	bool exists_dumpfile = std::ifstream("dump_native_cpp.csv").good();
	std::ofstream dumpfile("dump_native_cpp.csv", std::ofstream::out | std::ofstream::app);
	if (!exists_dumpfile)
	{
		for (auto data : time_execution_data)
		{
			dumpfile << data.first << ",";
		}
		dumpfile << std::endl;
		dumpfile.flush();
	}

	for (auto data : time_execution_data)
	{
		dumpfile << data.second << ",";
	}
	dumpfile << std::endl;
	dumpfile.flush();
	dumpfile.close();
#endif /* VTK_BENCHMARK_NATIVE */

#ifdef VTK_BENCHMARK_INTROSPECTION
	timed_execution_v("main", test_introspection);

	// Setup
	bool exists_dumpfile = std::ifstream("dump_introspection_cpp.csv").good();
	std::ofstream dumpfile("dump_introspection_cpp.csv", std::ofstream::out | std::ofstream::app);
	if (!exists_dumpfile)
	{
		for (auto data : time_execution_data)
		{
			dumpfile << data.first << ",";
		}
		dumpfile << std::endl;
		dumpfile.flush();
	}

	for (auto data : time_execution_data)
	{
		dumpfile << data.second << ",";
	}
	dumpfile << std::endl;
	dumpfile.flush();
	dumpfile.close();
#endif /* VTK_BENCHMARK_INTROSPECTION */

	return 0;
}

 