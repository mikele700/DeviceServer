#include <cstddef>
#include <iterator>
#include <mutex>
#include <string>
#include "physical_device.h"
#include <vector>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <condition_variable>
#include <iomanip>


class Device
{
    public:
        Device(int physical_id)
        {
            m_name = std::unique_ptr<Name>(new Name);
            m_params = std::unique_ptr<Parameters>(new Parameters);
            std::lock_guard<std::mutex> guard(*lib::getMutex());
            m_physical_device = std::unique_ptr<lib::Physical_device>(new lib::Physical_device(lib::getHardware(physical_id)));
            m_some_factor     = lib::getDefaultFactor();
        }

        Device(int physical_id, int fixed_factor)
        {
            m_some_factor = fixed_factor;
            m_name = std::unique_ptr<Name>(new Name);
            m_params = std::unique_ptr<Parameters>(new Parameters);
            std::lock_guard<std::mutex> guard(*lib::getMutex());
            m_physical_device = std::unique_ptr<lib::Physical_device>(new lib::Physical_device(lib::getHardware(physical_id)));
        }       

        std::string getName()
        {
            return m_name->_name;
        }

        void setName(std::string name)
        {
            std::lock_guard<std::mutex> guard(m_name->_mutex);
            m_name->_name = name;
        }       

        std::multiset<int> getParameters()
        {
            return m_params->_params;
        }

        void setParameters(std::multiset<int> iParameters)
        {
            std::lock_guard<std::mutex> guard(m_params->_mutex);
            m_params->_params = iParameters;
        }

    private:

        struct Name
        {
            std::string _name;
            std::mutex _mutex;
        };
        struct Parameters
        {
            std::multiset<int> _params;
            std::mutex _mutex;
        };

        int                                     m_some_factor;
        std::unique_ptr<lib::Physical_device>   m_physical_device;
        std::unique_ptr<Name>                   m_name;
        std::unique_ptr<Parameters>             m_params;
};





class Action
{
    public:

        Action()
        {
        }    

        // getResult: prints the result
        void getResult()
        {
            //it waits for the end of the command execution
            std::unique_lock<std::mutex> locker(_mutexExecution);
            _commandExecution.wait(locker, [this](){return resultReady;});
            printResult();
        }

        void printResult()
        {
            //thread-safe print
            std::lock_guard<std::mutex> guard(_mutexPrint);
            //getResult should probably print just the result, but I added the command too for testing reasons.
            std::cout << std::left  << std::setw(50) << _command << "\t" << std::left  << std::setw(20) << _result << std::endl;
        }

    private:
        std::string _command;
        std::string _result;
        std::mutex _mutexExecution;
        static std::mutex _mutexPrint;
        bool resultReady;
        std::condition_variable _commandExecution;

        //Worker will send the signal on the condition variable
        friend class Worker;
    
};

std::mutex Action::_mutexPrint;




class Worker
{
    public:

        Worker(std::shared_ptr<Action> iAction, std::shared_ptr<std::vector<Device>> iDevices): _action{iAction}, _devices{iDevices}
        {
        }

        //operator(): completes the task of processing the command
        void operator()(std::string iCommand)
        {
            std::unique_lock<std::mutex> locker(_action->_mutexExecution);
            _action->_command = iCommand;
            std::vector<std::string> parser = commandParsing(iCommand);
            
            try
            {
                _userCommand = commandRecognition(parser);
                _id = idRecognition(parser[2]);
                _action->_result = execute();
            }
            catch (char const* exception)
            {
                _action->_result = std::string(exception);
            }

            _action->resultReady = true;
            locker.unlock();
            _action->_commandExecution.notify_one();
        }

    private:

        std::shared_ptr<Action> _action;
        enum userCommand{setNameCommand, getNameCommand, setParametersCommand, getParametersCommand};
        userCommand _userCommand;
        int _id;
        std::string _name;
        std::multiset<int> _parameters;
        std::shared_ptr<std::vector<Device>> _devices;

        //commandParsing: parses the user command, using the white space as a delimite, and gives back the parsed vector
        std::vector<std::string> commandParsing(std::string iCommand)
        {
            std::string token;
            size_t pos;
            std::vector<std::string> parser;

            //Command parsing
            while((pos = iCommand.find(" ")) != std::string::npos)
            {
                token = iCommand.substr(0, pos);
                parser.push_back(token);
                iCommand.erase(0, pos + 1);
            }
            parser.push_back(iCommand);
            return parser;
        }

        //commandRecognition: validates and determines the command to execute
        userCommand commandRecognition(const std::vector<std::string> iCommand)
        {
            //Command: s
            if((iCommand.size() == 4) && (iCommand[0] == "s"))
            {
                //Command: s name
                if((iCommand[1] == "name") && nameRecognition(iCommand[3]))
                {
                    return setNameCommand;
                }
                // Command: s params
                else if((iCommand[1] == "params") && parametersRecognition(iCommand[3]))
                {
                    return setParametersCommand;
                }
            }
            //Command: g
            else if((iCommand.size() == 3) && (iCommand[0] == "g"))
            {
                //Command: g name
                if(iCommand[1] == "name")
                {
                    return getNameCommand;
                }
                // Command g params
                else if(iCommand[1] == "params")
                {
                    return getParametersCommand;
                }
            }
            throw "Error";
        }

        //idRecognition: validates and extracts the id
        int idRecognition(const std::string iId)
        {
            //Empty id check
            if(iId.empty())
            {
                throw "Error";
            }
            for(char digit : iId)
            {
                //Digit check
                if(!isdigit(digit))
                {
                    throw "Error";
                }
            }
            return std::stoi(iId);
        }

        //nameRecognition: validates and extracts the name. True in case of success
        bool nameRecognition(const std::string iName)
        {
            //Empty name check
            if(iName.empty())
            {
                return false;
            }
            for(char character : iName)
            {
                //Lower-case letter, digit and underscore checks
                if(!islower(character) && !isdigit(character) && character != '_'){
                    return false;
                }
            }
            _name = iName;
            return true;
        }

        //parametersRecognition: validates and extracts the parameters. True in case of success
        bool parametersRecognition(std::string iParameters)
        {
            std::string parameter;
            int intParameter;
            size_t pos;
            //Parameters parsing
            while((pos = iParameters.find(",")) != std::string::npos)
            {
                parameter = iParameters.substr(0, pos);
                //Empty parameter check
                if(parameter.empty())
                {
                    return false;
                }
                //Integer check
                for(const char& digit : parameter)
                {
                    if(!isdigit(digit))
                    {
                        return false;
                    }
                }
                intParameter = std::stoi(parameter);
                //Range check
                if(intParameter > 255)
                {
                    return false;
                }
                _parameters.insert(intParameter);
                iParameters.erase(0, pos + 1);
            }
            //Last parameter
            if(iParameters.empty())
            {
                return false;
            }
            for(const char& digit : iParameters)
            {
                if(!isdigit(digit))
                {
                    return false;
                }
            }
            intParameter = std::stoi(iParameters);
            if(intParameter > 255)
            {
                return false;
            }            
            _parameters.insert(intParameter);
            return true;    
        }

        //execute: complete the execution and gives back the result
        std::string execute()
        {
            std::string result;
            switch(_userCommand)
            {
                case setNameCommand:

                    if(!setName(_id, _name))
                    {
                        throw "Error";
                    }
                    else
                    {
                        result = "";
                    }
                    break;

                case getNameCommand:

                    result = getName(_id);
                    break;

                case setParametersCommand:

                    if(!setParameters(_id, _parameters))
                    {
                        throw "Error";
                    }
                    else
                    {
                        result = "";
                    }
                    break;

                case getParametersCommand:

                    result = getParameters(_id);
                    break;

                default:

                    throw "Error";
            }
            return result;
        }

        bool setName(int iId, std::string iName)
        {
            if(iId < _devices->size())
            {
                _devices->at(iId).setName(iName);
                return true;
            }
            return false;
        }

        std::string getName(int iId)
        {
            if(iId < _devices->size())
            {
                std::string name = _devices->at(iId).getName();
                if(name.empty())
                {
                    return std::to_string(iId);
                }
                else
                {
                    return name;
                }
            }
            return "Error";            
        }

        bool setParameters(int iId, std::multiset<int> iParameters)
        {
            if(iId < _devices->size())
            {
                _devices->at(iId).setParameters(iParameters);
                return true;
            }
            return false;            
        }

        std::string getParameters(int iId)
        {
            if(iId < _devices->size())
            {
                std::multiset<int> parametersSet = _devices->at(iId).getParameters();
                std::string parametersString = "";
                for (std::multiset<int>::iterator it = parametersSet.begin(); it != parametersSet.end(); ++it)
                {
                    if(it == parametersSet.begin())
                    {
                        parametersString = (std::to_string(*it));
                    }
                    else
                    {
                        parametersString += ("," + std::to_string(*it));
                    }
                }
                return parametersString;
            }
            return "Error";            
        }

};




//Singleton
class Server
{
    public:

        Server(Server&) = delete;
        void operator=(const Server&) = delete;

        static Server* GetInstance();

        void addDevice(Device&& iDevice)
        {
            std::lock_guard<std::mutex> guard(_deviceMux);
            _devices->push_back(std::move(iDevice));
        }

        std::shared_ptr<Action> createAction(std::string iCommand)
        {   
            std::shared_ptr<Action> createdAction = std::shared_ptr<Action>(new Action());
            std::thread worker(Worker(createdAction, _devices), std::move(iCommand));
            worker.detach();
            return createdAction;
        }
        //getStatus prints the status of the devices for testing reasons. It's not protected by a mux because it's not part of the algorithm and used only after the join with the threads.
        void getStatus()
        {
            std::cout << std::endl << "*** DEVICES ***" << std::endl;
            int i = 0;
            for (std::vector<Device>::iterator it = _devices->begin() ; it != _devices->end(); ++it)
            {
                std::multiset<int> parametersSet = it->getParameters();
                std::string parametersString = "";
                for (std::multiset<int>::iterator it=parametersSet.begin(); it!=parametersSet.end(); ++it)
                {
                    if(it == parametersSet.begin())
                    {
                        parametersString = (std::to_string(*it));
                    }
                    else
                    {
                        parametersString += ("," + std::to_string(*it));
                    }
                }
                std::cout << i << std::left  << std::setw(1) << "\t" << std::left  << std::setw(10) << it->getName() << "\t" << std::left  << std::setw(20) << parametersString<< std::endl;
                i++;
            }
        }

    private:

        static Server* _server;
        static std::mutex _serverMux;
        std::shared_ptr<std::vector<Device>> _devices;
        std::mutex _deviceMux;

        Server()
        {
            _devices = std::shared_ptr<std::vector<Device>>(new std::vector<Device>());
        }

        ~Server()
        {
        }

};

Server* Server::_server{nullptr};
std::mutex Server::_serverMux;

Server* Server::GetInstance()
{
    std::lock_guard<std::mutex> lock(_serverMux);
    if(_server == nullptr)
    {
        _server = new Server();
    }
    return _server;
}

void FirstThread()
{
    Server* server = Server::GetInstance();
    server->createAction("s name 1  ")->getResult();                            // = <Error>
    server->createAction("s naame 0 pc_magnet_1")->getResult();                 // = <Error>
    server->createAction("randomletters")->getResult();                         // = <Error>
    server->createAction("s name 1 some_name")->getResult();                    // = ""
    server->createAction("g name 1")->getResult();                              // = "some_name"
    server->createAction("g params 3")->getResult();                            // = <Error>
    server->createAction("s params 3 4,3,2,1,0,44,55,44,55")->getResult();      // = <Error>

}

void SecondThread()
{
    Server* server = Server::GetInstance();
    server->createAction("s name 1 some-name!")->getResult();                   // = <Error>
    server->createAction("s name -1 pc_magnet_1")->getResult();                 // = <Error>
    server->createAction("s params 0 4,3,,2")->getResult();                     // = <Error>
    server->createAction("s name 3 some_name")->getResult();                    // = <Error>
    server->createAction("g name 3")->getResult();                              // = <Error>
    server->createAction("s params 0 4,3,2,1,0,44,55,44,55")->getResult();      // = ""
    server->createAction("g params 0")->getResult();                            // = "0,1,2,3,4,44,44,55,55"    

}

void ThirdThread()
{
    Server* server = Server::GetInstance();
    server->addDevice(Device(98));
    server->addDevice(Device(99));

}



// Sample main() functon - might be useful for quick testing 
int main()
{
    Server* server = Server::GetInstance();

    std::thread t1(FirstThread);
    std::thread t2(SecondThread);
    std::thread t3(ThirdThread);

    
    server->addDevice(Device(97));

    t3.join();
    
    server->createAction("s name  ")->getResult();                              // = <Error>
    server->createAction("s name 1 Some_name")->getResult();                    // = <Error>
    server->createAction("g name notanumber")->getResult();                     // = <Error>
    server->createAction("s params 0 4,3,2,1,0,44,55,44,55,9999")->getResult(); // = <Error>    
    server->createAction("s name 0 pc_magnet_1")->getResult();                  // = ""
    server->createAction("g name 0")->getResult();                              // = "pc_magnet_1"
    server->createAction("g name 2")->getResult();                              // = "2"
    server->createAction("s params 2 6,4,19,95")->getResult();                  // = "6,4,19,95"
    
    t1.join();
    t2.join();

    server->getStatus();
}
