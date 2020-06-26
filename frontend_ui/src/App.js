import React, {Component} from 'react';

import Preview from './components/Preview';
import StatusBar from './components/StatusBar';
import DataTable from './components/DataTable';
import DetectorGrid from './components/DetectorGrid';

function formatTime(val) {
    var x = Number(val);
    return (x < 0.001)?((x*1000000).toPrecision(3) + " us"):((x*1000).toPrecision(3) + " ms");   
}

function handleErrors(response) {
    if (!response.ok) {
        throw Error(response.statusText);
    }
    return response;
}

class App extends Component {
  state = {
     connected: false,
     expertMode: false
  }

  updateREST() {
    //fetch('http://' + window.location.hostname + '/jf/config', {crossDomain:true})
      fetch('/jf/config')
    .then(handleErrors)
    .then(res => res.json())
    .then(data => {
         this.setState({ daqState: data.state});
         this.setState({ connected: true});
         this.setState({ params: [
              {desc: "Frame time", value: formatTime(data.frame_time)},
              {desc: "Frame time (internal)", value: formatTime(data.frame_time_detector/1e6)},
              {desc: "Count time ", value: formatTime(data.count_time)},
              {desc: "Count time (internal)", value: formatTime(data.count_time_detector/1e6)},
              {desc: "Frame summation", value: data.summation},
              {desc: "Compression", value: data.compression},
              {desc: "Beam center x", value: data.beam_center_x + " pxl"},
              {desc: "Beam center y", value: data.beam_center_y + " pxl"},
              {desc: "Detector distance", value: data.detector_distance + " mm"},
              {desc: "Beamline delay (max)", value: formatTime(data.beamline_delay)},
              {desc: "Shutter delay", value: formatTime(data.shutter_delay)},
              {desc: "Energy", value: data.photon_energy + " keV"},
              {desc: "Pedestal G0 frames", value: data.pedestalG0_frames},
              {desc: "Pedestal G1 frames", value: data.pedestalG1_frames},
              {desc: "Pedestal G2 frames", value: data.pedestalG2_frames},
              {desc: "Tracking ID", value: data.trackingID},
              {desc: "Best resolution (full circle)", value: data.resolution_limit_edge}
              ]});
         var i;
         var in_modules = [];
         for (i = 0; i < 8; i++) {
             var z = 2 * ( 3 - Math.floor(i / 2)) + i % 2;
            in_modules.push({name: "Module " + z,
                bad_pixels: data.bad_pixels[z],
                meanG0: data.pedestalG0_mean[z],
                meanG1: data.pedestalG1_mean[z],
                meanG2: data.pedestalG2_mean[z]})
         }
         this.setState({modules: in_modules});
    })
    .catch(error => {
        this.setState({value: {connected: false}})
     });
     this.setState({contrast : this.state.contrast + 1});
  }
  
  componentDidMount() { 
    this.updateREST();
    this.interval = setInterval(() => this.updateREST(), 1000);
  }

  componentWillUnmount() {
    clearInterval(this.interval);
  }

  sliderMoved = (event, newValue) => {
    this.setState({contrast: newValue});
  };

  handleChange = (event) => {
    this.setState({[event.target.name]: event.target.checked });
  };

  render() {
    return <div>
        {this.state.connected?<div>
            <StatusBar daqState={this.state.daqState} handleChange={this.handleChange} expertMode={this.state.expertMode} />
            <br/><br/>
            {this.state.expertMode?
                <div><DataTable params={this.state.params}/><br/><br/>
                <DetectorGrid modules={this.state.modules}/><br/><br/>
                <iframe title="grafana" src="http://mx-jungfrau-1:3000/d-solo/npmZQ7kMk/servers?orgId=1&theme=light&panelId=12" width="100%" height="250" frameBorder="0"/>
                </div>
            :
                <Preview/>}
            <br/></div>
         :
            <h1> Detector server not running </h1>}
     </div>
  }
}

export default App;
