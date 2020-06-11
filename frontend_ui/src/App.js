import React, {Component} from 'react';

import Preview from './components/Preview';
import StatusBar from './components/StatusBar';
import DataTable from './components/DataTable';
import DetectorGrid from './components/DetectorGrid';

function formatTime(val) {
    var x = Number(val);
    return (x < 0.001)?((x*1000000).toPrecision(3) + " us"):((x*1000).toPrecision(3) + " ms");   
}

class App extends Component {
  state = {
     value: {state: "Not connected"},
     expertMode: false,
  }

  updateREST() {
    fetch('http://' + window.location.hostname + ':5232/', {crossDomain:true})
    .then(res => res.json())
    .then(data => {
         this.setState({ daqState: data.state});
         this.setState({ params: [
              {desc: "Frame time", value: formatTime(data.frame_time)},
              {desc: "Frame time (internal)", value: formatTime(data.frame_time_detector)},
              {desc: "Count time ", value: formatTime(data.count_time)},
              {desc: "Count time (internal)", value: formatTime(data.count_time_detector)},
              {desc: "Frame summation", value: data.summation},
              {desc: "Compression", value: data.compression},
              {desc: "Beam center x", value: data.beam_center_x + " pxl"},
              {desc: "Beam center y", value: data.beam_center_y + " pxl"},
              {desc: "Detector distance", value: data.detector_distance + " mm"},
              {desc: "Beamline delay (max)", value: formatTime(data.beamline_delay)},
              {desc: "Shutter delay", value: formatTime(data.shutter_delay)},
              {desc: "Energy", value: data.energy_in_keV + " keV"},
              {desc: "Pedestal G0 frames", value: data.pedestalG0_frames},
              {desc: "Pedestal G1 frames", value: data.pedestalG1_frames},
              {desc: "Pedestal G2 frames", value: data.pedestalG2_frames},
              ]});
         this.setState({modules: [
              {name: "Module 6", 
               bad_pixels: data.bad_pixels_mod6, 
               meanG0: data.pedestalG0_mean_mod6,  
               meanG1: data.pedestalG1_mean_mod6,  
               meanG2: data.pedestalG2_mean_mod6},
              {name: "Module 7", 
               bad_pixels: data.bad_pixels_mod7, 
               meanG0: data.pedestalG0_mean_mod7,  
               meanG1: data.pedestalG1_mean_mod7,  
               meanG2: data.pedestalG2_mean_mod7},
              {name: "Module 4", 
               bad_pixels: data.bad_pixels_mod4, 
               meanG0: data.pedestalG0_mean_mod4,  
               meanG1: data.pedestalG1_mean_mod4,  
               meanG2: data.pedestalG2_mean_mod4},
              {name: "Module 5", 
               bad_pixels: data.bad_pixels_mod5, 
               meanG0: data.pedestalG0_mean_mod5,  
               meanG1: data.pedestalG1_mean_mod5,  
               meanG2: data.pedestalG2_mean_mod5},
              {name: "Module 3", 
               bad_pixels: data.bad_pixels_mod3, 
               meanG0: data.pedestalG0_mean_mod3,  
               meanG1: data.pedestalG1_mean_mod3,  
               meanG2: data.pedestalG2_mean_mod3},
              {name: "Module 2", 
               bad_pixels: data.bad_pixels_mod2, 
               meanG0: data.pedestalG0_mean_mod2,  
               meanG1: data.pedestalG1_mean_mod2,  
               meanG2: data.pedestalG2_mean_mod2},
              {name: "Module 1", 
               bad_pixels: data.bad_pixels_mod1, 
               meanG0: data.pedestalG0_mean_mod1,  
               meanG1: data.pedestalG1_mean_mod1,  
               meanG2: data.pedestalG2_mean_mod1},
              {name: "Module 0", 
               bad_pixels: data.bad_pixels_mod0, 
               meanG0: data.pedestalG0_mean_mod0,  
               meanG1: data.pedestalG1_mean_mod0,  
               meanG2: data.pedestalG2_mean_mod0},
            ]});
    })
    .catch(error => {
        this.setState({value: {state: "Not connected"}})
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
    return <div><StatusBar daqState={this.state.daqState} handleChange={this.handleChange} expertMode={this.state.expertMode} />
     <br/><br/>

     {this.state.expertMode?
     <div><DataTable params={this.state.params}/><br/><br/>
     <DetectorGrid modules={this.state.modules}/><br/><br/>
     <iframe title="grafana" src="http://mx-jungfrau-1:3000/d-solo/npmZQ7kMk/servers?orgId=1&theme=light&panelId=12" width="100%" height="250" frameBorder="0"/>
     </div>
    :<Preview></Preview>}
    <br/>

     </div>
  }
}

export default App;
